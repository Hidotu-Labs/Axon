"""Offline regression tests for native lifecycle and rootfs operations."""
import http.server
import importlib.util
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import threading
import unittest

REPO = Path(__file__).resolve().parents[1]
AXON = REPO / 'axon'


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
        self.assertFalse((self.root / 'var/lib/pacman/local/lifecycle-app-1.0').exists())

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
        self.write('var/lib/pacman/local/library-1.0/desc', '%NAME%\nlibrary\n\n%VERSION%\n1.0\n')
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
        self.write('var/lib/pacman/local/provider-1/desc', '%NAME%\nprovider\n\n%VERSION%\n1\n\n%PROVIDES%\nvirtual=2\n')
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

    def test_pacman_interoperability_and_file_ownership(self):
        # Create package with file
        self.package('cjson-demo', {'usr/lib/libcjson.so.1': 'cjson-binary-data'}, version='1.7.18')
        # Install via Axon
        self.run_axon('get', '--noconfirm', '--keep-cache', 'cjson-demo')
        target_file = self.root / 'usr/lib/libcjson.so.1'
        self.assertTrue(target_file.exists())
        self.assertEqual(target_file.read_text(), 'cjson-binary-data')

        # Pacman -Q query
        pacman_q = subprocess.run(['pacman', '--root', str(self.root), '-Q', 'cjson-demo'],
                                  capture_output=True, text=True)
        self.assertEqual(pacman_q.returncode, 0, pacman_q.stderr)
        self.assertIn('cjson-demo 1.7.18-1', pacman_q.stdout)

        # Pacman -Qo file ownership query
        pacman_qo = subprocess.run(['pacman', '--root', str(self.root), '-Qo', str(target_file)],
                                   capture_output=True, text=True)
        self.assertEqual(pacman_qo.returncode, 0, pacman_qo.stderr)
        self.assertIn('is owned by cjson-demo 1.7.18-1', pacman_qo.stdout)

        # Remove via Axon
        self.run_axon('rm', '--noconfirm', 'cjson-demo')
        self.assertFalse(target_file.exists())

        # Pacman -Q should now report package not found
        pacman_q_after = subprocess.run(['pacman', '--root', str(self.root), '-Q', 'cjson-demo'],
                                        capture_output=True, text=True)
        self.assertNotEqual(pacman_q_after.returncode, 0)

    def test_parallel_downloads(self):
        server_dir = self.base / 'http_server'
        server_dir.mkdir()

        for name, content in [('multi-a', 'data-a'), ('multi-b', 'data-b'), ('multi-c', 'data-c')]:
            archive = server_dir / f'{name}.tar'
            with tarfile.open(archive, 'w') as tar:
                payload = content.encode()
                entry = tarfile.TarInfo(f'usr/share/{name}')
                entry.size = len(payload)
                entry.mode = 0o644
                tar.addfile(entry, io.BytesIO(payload))

        class QuietHandler(http.server.SimpleHTTPRequestHandler):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, directory=str(server_dir), **kwargs)
            def log_message(self, format, *args):
                pass

        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), QuietHandler)
        port = server.server_address[1]
        t = threading.Thread(target=server.serve_forever, daemon=True)
        t.start()
        try:
            for name in ['multi-a', 'multi-b', 'multi-c']:
                archive = server_dir / f'{name}.tar'
                with self.manifest.open('a') as stream:
                    stream.write(f'{name}|1.0|{archive.stat().st_size}|http://127.0.0.1:{port}/{archive.name}|fixture|\n')

            res = self.run_axon('get', '--noconfirm', '--parallel', '3', 'multi-a', 'multi-b', 'multi-c')
            self.assertIn('Retrieving 3 packages (3 concurrent downloads)...', res.stdout)
            self.assertEqual((self.root / 'usr/share/multi-a').read_text(), 'data-a')
            self.assertEqual((self.root / 'usr/share/multi-b').read_text(), 'data-b')
            self.assertEqual((self.root / 'usr/share/multi-c').read_text(), 'data-c')
            self.assertTrue((self.root / 'var/lib/pacman/local/multi-a-1.0-1/desc').is_file())
            self.assertTrue((self.root / 'var/lib/pacman/local/multi-b-1.0-1/desc').is_file())
            self.assertTrue((self.root / 'var/lib/pacman/local/multi-c-1.0-1/desc').is_file())

            # Test ParallelDownloads loaded from axon.conf
            self.conf.write_text('[options]\nParallelDownloads = 2\n[fixture]\nServer = file:///unused\n')
            for name, content in [('multi-d', 'data-d'), ('multi-e', 'data-e')]:
                archive = server_dir / f'{name}.tar'
                with tarfile.open(archive, 'w') as tar:
                    payload = content.encode()
                    entry = tarfile.TarInfo(f'usr/share/{name}')
                    entry.size = len(payload)
                    entry.mode = 0o644
                    tar.addfile(entry, io.BytesIO(payload))
                with self.manifest.open('a') as stream:
                    stream.write(f'{name}|1.0|{archive.stat().st_size}|http://127.0.0.1:{port}/{archive.name}|fixture|\n')

            res2 = self.run_axon('get', '--noconfirm', 'multi-d', 'multi-e')
            self.assertIn('Retrieving 2 packages (2 concurrent downloads)...', res2.stdout)
            self.assertEqual((self.root / 'usr/share/multi-d').read_text(), 'data-d')
            self.assertEqual((self.root / 'usr/share/multi-e').read_text(), 'data-e')
        finally:
            server.shutdown()
            server.server_close()


if __name__ == '__main__':
    unittest.main()
