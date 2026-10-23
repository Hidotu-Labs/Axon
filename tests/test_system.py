"""Offline regression tests for native lifecycle and rootfs operations."""
import importlib.util
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
AXON = REPO / 'axon'
spec = importlib.util.spec_from_file_location('axon_runtime', REPO / 'libexec/axon-runtime.py')
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


class SystemTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='axon-system-')
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base / 'root'
        self.root.mkdir()
        self.sync = self.root / 'var/lib/axon/sync'
        self.cache = self.root / 'var/cache/axon/pkg'
        self.sync.mkdir(parents=True)
        self.cache.mkdir(parents=True)
        self.conf = self.base / 'axon.conf'
        self.conf.write_text('[fixture]\nServer = file:///unused\n')
        self.manifest = self.sync / 'fixture.txt'
        self.manifest.touch()
        self.env = {**os.environ, 'AXON_CONF': str(self.conf), 'NO_COLOR': '1'}
        self.env.pop('AXON_SYNC_PATH', None)

    def run_axon(self, *args, success=True):
        result = subprocess.run([str(AXON), '--root', str(self.root), *map(str, args)],
                                env=self.env, text=True, capture_output=True, timeout=30)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def write(self, name, data, mode=0o644):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(data)
        path.chmod(mode)
        return path

    def shell(self):
        # Use the host's trusted bash and the shared libraries named by its loader.
        paths = ['/bin/bash']
        output = subprocess.check_output(['ldd', '/bin/bash'], text=True)
        paths += re.findall(r'(/[^\s()]+)', output)
        for item in paths:
            source = Path(item)
            destination = self.root / item.lstrip('/')
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)

    def package(self, name, files=None, depends='', version='1.0'):
        archive = self.cache / (name + '.tar')
        with tarfile.open(archive, 'w') as tar:
            for path, data in (files or {}).items():
                entry = tarfile.TarInfo(path)
                payload = data.encode()
                entry.size = len(payload)
                entry.mode = 0o755 if path.startswith(('usr/bin/', 'bin/')) else 0o644
                tar.addfile(entry, io.BytesIO(payload))
        with self.manifest.open('a') as stream:
            stream.write(f'{name}|{version}|{archive.stat().st_size}|https://invalid.example/{archive.name}|fixture|{depends}\n')

    def hook(self, name, phase, command, targets='Target = lifecycle-app', kind='Package', extra=''):
        self.write('usr/share/libalpm/hooks/' + name,
                   f'[Trigger]\nOperation = Install\nType = {kind}\n{targets}\n'
                   f'[Action]\nWhen = {phase}\nExec = {command}\n{extra}\n')

    def test_scriptlets_and_hooks_really_run_in_target(self):
        self.shell()
        self.hook('01-pre.hook', 'PreTransaction', "/bin/bash -c 'printf hook-pre\\n >> /events'")
        # The overridden packaged hook would abort; /dev/null must mask it.
        self.hook('02-disabled.hook', 'PreTransaction', '/bin/bash -c "exit 9"', extra='AbortOnFail')
        disabled = self.root / 'etc/pacman.d/hooks/02-disabled.hook'
        disabled.parent.mkdir(parents=True)
        disabled.symlink_to('/dev/null')
        self.hook('90-post.hook', 'PostTransaction',
                  '''/bin/bash -c 'while IFS= read -r line; do printf "path:%s\\n" "$line" >> /events; done' ''',
                  targets='Target = usr/share/*\nTarget = !usr/share/skip*', kind='Path', extra='NeedsTargets')
        script = '''pre_install() { printf 'pre:%s\\n' "$1" >> /events; }
post_install() { printf 'post:%s\\n' "$1" >> /events; }
'''
        self.package('lifecycle-app', {'.INSTALL': script, 'usr/share/keep': 'yes', 'usr/share/skip-this': 'no'})
        self.run_axon('get', '--noconfirm', '--keep-cache', 'lifecycle-app')
        events = (self.root / 'events').read_text()
        self.assertIn('pre:1.0', events)
        self.assertIn('post:1.0', events)
        self.assertIn('path:usr/share/keep', events)
        self.assertNotIn('path:usr/share/skip-this', events)
        self.assertLess(events.index('hook-pre'), events.index('pre:1.0'))
        self.assertLess(events.index('post:1.0'), events.index('path:usr/share/keep'))

    def test_pre_hook_aborts_before_extraction(self):
        self.shell()
        self.hook('01-fail.hook', 'PreTransaction', '/bin/bash -c "exit 19"', extra='AbortOnFail')
        self.package('lifecycle-app', {'usr/share/no-write': 'no'})
        self.run_axon('get', '--noconfirm', 'lifecycle-app', success=False)
        self.assertFalse((self.root / 'usr/share/no-write').exists())
        self.assertFalse((self.root / 'var/lib/axon/local/lifecycle-app-1.0').exists())

    def test_scriptlet_failure_propagates(self):
        self.shell()
        self.package('script-fails', {'.INSTALL': 'pre_install() { return 23; }\n', 'usr/share/no-write': 'no'})
        self.run_axon('get', '--noconfirm', 'script-fails', success=False)
        self.assertFalse((self.root / 'usr/share/no-write').exists())

    def test_post_hook_failure_reports_installed_state(self):
        self.shell()
        self.hook('99-fail.hook', 'PostTransaction', '/bin/bash -c "exit 17"')
        self.package('lifecycle-app', {'usr/share/installed-despite-post-failure': 'yes'})
        self.run_axon('get', '--noconfirm', 'lifecycle-app', success=False)
        self.assertTrue((self.root / 'usr/share/installed-despite-post-failure').exists())

    def test_incompatible_installed_dependency_fails_before_download(self):
        self.write('var/lib/axon/local/library-1.0/desc', '%NAME%\nlibrary\n\n%VERSION%\n1.0\n')
        self.package('requires-new', {'usr/share/should-not-exist': 'no'}, depends='library>=2')
        result = self.run_axon('get', '--noconfirm', 'requires-new', success=False)
        self.assertIn('does not satisfy library>=2', result.stderr)
        self.assertFalse((self.root / 'usr/share/should-not-exist').exists())

    def test_new_dependency_versions_and_cycles(self):
        self.package('library', {'usr/share/library': 'ok'}, version='2.0')
        self.package('needs-new', {'usr/share/needs-new': 'ok'}, depends='library>=2')
        self.run_axon('get', '--noconfirm', '--keep-cache', 'needs-new')
        self.package('requires-3', {'usr/share/no-3': 'no'}, depends='library>=3')
        self.run_axon('get', '--noconfirm', 'requires-3', success=False)
        self.assertFalse((self.root / 'usr/share/no-3').exists())
        self.package('cycle-a', {'usr/share/cycle-a': 'ok'}, depends='cycle-b>=1')
        self.package('cycle-b', {'usr/share/cycle-b': 'ok'}, depends='cycle-a>=1')
        self.run_axon('get', '--noconfirm', 'cycle-a')

    def test_versioned_installed_virtual_provider(self):
        self.write('var/lib/axon/local/provider-1/desc', '%NAME%\nprovider\n\n%VERSION%\n1\n\n%PROVIDES%\nvirtual=2\n')
        self.package('uses-virtual', {'usr/share/virtual-ok': 'ok'}, depends='virtual>=2')
        self.run_axon('get', '--noconfirm', 'uses-virtual')
        self.assertTrue((self.root / 'usr/share/virtual-ok').exists())

    def test_caches_execute_target_tools(self):
        self.shell()
        for name in ('ldconfig', 'fc-cache', 'gtk-update-icon-cache', 'glib-compile-schemas'):
            self.write('usr/bin/' + name, '#!/bin/bash\nprintf "%s\\n" "$0 $*" >> /cache-events\n', 0o755)
        self.write('usr/share/icons/hicolor/index.theme', '[Icon Theme]\nName=hicolor\n')
        (self.root / 'usr/share/glib-2.0/schemas').mkdir(parents=True)
        self.run_axon('cache')
        events = (self.root / 'cache-events').read_text()
        self.assertIn('gtk-update-icon-cache -f -t /usr/share/icons/hicolor', events)
        self.assertIn('glib-compile-schemas /usr/share/glib-2.0/schemas', events)

    def test_map_and_minimal_rootfs_command(self):
        result = self.run_axon('map', 'qt6-qtbase', 'libpng-dev', 'openrc')
        self.assertEqual(result.stdout.splitlines(), ['qt6-base', 'DROP', 'DROP'])
        for name in ('filesystem', 'glibc', 'busybox', 'coreutils', 'sed', 'grep', 'gawk', 'tar', 'gzip', 'xz', 'util-linux', 'shadow'):
            self.package(name)
        self.shell()
        self.package('bash', {'.INSTALL': 'post_install() { printf replayed > /bootstrap-replay; }\n'})
        self.package('extra-payload', {'usr/share/rootfs-extra': 'ok'})
        packages = self.base / 'packages.txt'
        packages.write_text('extra-payload\n')
        self.run_axon('rootfs', '--no-caches', '--packages-from', packages)
        self.assertEqual((self.root / 'usr/share/rootfs-extra').read_text(), 'ok')
        self.assertEqual((self.root / 'bootstrap-replay').read_text(), 'replayed')
        self.assertTrue((self.root / 'etc/mtab').is_symlink())
        self.assertEqual((self.root / 'tmp').stat().st_mode & 0o7777, 0o1777)

    def test_image_contains_files_and_refuses_accidental_overwrite(self):
        self.write('etc/passwd', 'root:x:0:0:root:/:/bin/bash\navory:x:1000:1000::/home/avory:/bin/bash\n')
        self.write('home/avory/example', 'user content')
        self.write('etc/test-payload', 'image data')
        self.write('var/cache/axon/pkg/excluded.tar', 'cached archive')
        image = self.base / 'rootfs.img'
        self.run_axon('image', '--output', image, '--size', '32M')
        content = subprocess.check_output(['debugfs', '-R', 'cat /etc/test-payload', str(image)], text=True, stderr=subprocess.DEVNULL)
        self.assertEqual(content, 'image data')
        cached = subprocess.check_output(['debugfs', '-R', 'cat /var/cache/axon/pkg/excluded.tar', str(image)], text=True, stderr=subprocess.DEVNULL)
        self.assertEqual(cached, '')
        self.assertTrue((self.root / 'var/cache/axon/pkg/excluded.tar').exists())
        stat = subprocess.check_output(['debugfs', '-R', 'stat /etc/test-payload', str(image)], text=True, stderr=subprocess.DEVNULL)
        self.assertRegex(stat, r'User:\s+0\s+Group:\s+0')
        stat = subprocess.check_output(['debugfs', '-R', 'stat /home/avory/example', str(image)], text=True, stderr=subprocess.DEVNULL)
        self.assertRegex(stat, r'User:\s+1000\s+Group:\s+1000')
        self.run_axon('image', '--output', image, '--size', '32M', success=False)
        self.run_axon('image', '--output', self.root / 'recursive.img', '--size', '32M', success=False)
        self.assertFalse((self.root / 'recursive.img').exists())

    def test_desktop_profile_accounts_pam_and_session_settings(self):
        self.shell()
        source = self.base / 'avory-source'
        assets = ['userland/avory-account', 'userland/test_accounts.sh', 'userland/avory-login.elf',
                  'config/vlc/vlc.sh', 'config/xorg/10-modesetting.conf', 'config/xorg/10-native.conf',
                  'config/xorg/10-nouveau.conf', 'config/xorg/20-output.conf', 'config/xorg/44-nouveau-env.sh',
                  'config/xorg/45-prime-output.sh', 'config/xorg/avory-nouveau-xorg.sh',
                  'config/xorg/avory-prime-display.sh', 'config/xorg/avory-xorg-profile.sh']
        for asset in assets:
            path = source / asset
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('# fixture asset\n')
        self.write('etc/passwd', 'root:x:0:0:root:/root:/bin/bash\n')
        self.write('etc/group', 'root:x:0:\n')
        self.write('etc/pacman.conf', '[options]\n')
        self.write('etc/pacman.d/mirrorlist', '# no enabled mirrors\n')
        self.write('usr/bin/startplasma-x11', '#!/bin/sh\nexit 0\n', 0o755)
        # A real ELF fixture exercises the target loader check, without Qt packages.
        plugin = self.root / 'usr/lib/qt6/plugins/platforms/libqxcb.so'
        plugin.parent.mkdir(parents=True)
        subprocess.run(['cc', '-shared', '-nostdlib', '-x', 'c', '-', '-o', str(plugin)],
                       input='int fixture(void) { return 0; }', text=True, check=True)
        loader = Path('/usr/lib/ld-linux-x86-64.so.2')
        if not loader.exists():
            loader = Path('/lib64/ld-linux-x86-64.so.2')
        shutil.copy2(loader, self.root / 'usr/lib/ld-linux-x86-64.so.2')
        # Repetition must preserve single account entries and working session wrappers.
        for _ in range(2):
            self.run_axon('configure', '--profile', 'desktop', '--source', source, '--no-caches')
        passwd = (self.root / 'etc/passwd').read_text()
        self.assertEqual(sum(row.startswith('avory:') for row in passwd.splitlines()), 1)
        self.assertIn('root::0:0:root:/:', passwd)
        self.assertIn('pam_permit.so', (self.root / 'etc/pam.d/sddm').read_text())
        self.assertIn('DisplayServer=x11', (self.root / 'etc/sddm.conf').read_text())
        self.assertIn('dbus-run-session', (self.root / 'usr/bin/startplasma-x11').read_text())
        self.assertTrue((self.root / 'etc/init.d/rcS').is_file())
        self.assertEqual(os.readlink(self.root / 'etc/mtab'), '/proc/self/mounts')

    def test_profile_assets_preflight(self):
        self.run_axon('configure', '--profile', 'desktop', '--source', self.base, success=False)
        self.assertFalse((self.root / 'etc/passwd').exists())
        subprocess.run(['bash', '-n', str(runtime.PROFILE / 'configure.sh')], check=True)
        subprocess.run(['bash', '-n', str(runtime.PROFILE / 'configure-accounts.sh')], check=True)

    def test_target_symlinks_do_not_resolve_into_host(self):
        (self.root / 'usr').symlink_to('/target-usr')
        self.assertEqual(runtime.target_path(self.root, '/usr/bin/tool'), self.root / 'target-usr/bin/tool')


if __name__ == '__main__':
    unittest.main()
