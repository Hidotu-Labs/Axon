#!/usr/bin/env python3
"""Axon target execution, ALPM install hooks, and AvoryOS rootfs commands."""
import argparse
import fnmatch
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

PROFILE = Path(__file__).resolve().parent / 'profiles/avory'


def target_path(root, relative):
    """Resolve target symlinks using target /, never the host's /usr or /etc."""
    root = Path(root).resolve()
    remaining = list(Path('/' + str(relative).lstrip('/')).parts[1:])
    parts = []
    links = 0
    while remaining:
        part = remaining.pop(0)
        if part in ('', '.'):
            continue
        if part == '..':
            if parts:
                parts.pop()
            continue
        candidate = root.joinpath(*parts, part)
        if candidate.is_symlink():
            links += 1
            if links > 40:
                raise ValueError(f'too many symlinks: {relative}')
            link = os.readlink(candidate)
            if link.startswith('/'):
                parts.clear()
            remaining = list(Path(link).parts[1:] if link.startswith('/') else Path(link).parts) + remaining
        else:
            parts.append(part)
    return root.joinpath(*parts)


def in_target(root, command, data=None):
    env = {**os.environ, 'HOME': '/', 'PATH': '/usr/bin:/usr/sbin:/bin:/sbin', 'LC_ALL': 'C'}
    created = []
    if str(root) == '/':
        argv = command
    else:
        # Bind only the four basic character devices in a private mount namespace.
        # Never fall back to executing a target command on the host.
        dev = root / 'dev'
        if dev.is_symlink():
            raise ValueError('target /dev must be a real directory')
        dev.mkdir(exist_ok=True)
        for device in ('null', 'zero', 'random', 'urandom'):
            path = dev / device
            if path.is_symlink():
                raise ValueError(f'target /dev/{device} must not be a symlink')
            if not path.exists():
                path.touch(); created.append(path)
        wrapper = ('root=$1; shift; '
                   'for device in null zero random urandom; do '
                   'mount --bind "/dev/$device" "$root/dev/$device" || exit 126; done; '
                   'exec chroot -- "$root" "$@"')
        argv = ['unshare', '-rm', '/bin/bash', '-c', wrapper, 'axon-target', str(root), *command]
    print(':: target:', shlex.join(command), flush=True)
    try:
        return subprocess.run(argv, input=data, text=True, env=env, check=False).returncode
    finally:
        for path in created:
            path.unlink(missing_ok=True)


def installed_names(root, db):
    names = set()
    for directory in (Path(db) / 'local', target_path(root, 'var/lib/pacman/local')):
        for desc in directory.glob('*/desc'):
            lines = desc.read_text().splitlines()
            if '%NAME%' in lines:
                index = lines.index('%NAME%') + 1
                if index < len(lines):
                    names.add(lines[index])
    return names


def read_hook(path):
    triggers, action, current = [], {}, None
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        if line == '[Trigger]':
            current = {}; triggers.append(current)
        elif line == '[Action]':
            if action:
                raise ValueError(f'{path}: duplicate Action')
            current = action
        elif line.startswith('[') or current is None:
            raise ValueError(f'{path}:{number}: invalid section')
        else:
            key, sep, value = line.partition('=')
            key, value = key.strip(), value.strip()
            allowed = {'Operation', 'Type', 'Target'} if current is not action else {
                'Description', 'When', 'Exec', 'Depends', 'AbortOnFail', 'NeedsTargets'}
            if key not in allowed or (not sep and key not in {'AbortOnFail', 'NeedsTargets'}):
                raise ValueError(f'{path}:{number}: unsupported hook directive: {line}')
            current.setdefault(key, []).append(value)
    if not triggers or not action.get('When') or not action.get('Exec'):
        raise ValueError(f'{path}: missing Trigger or Action fields')
    if len(action['When']) != 1 or action['When'][0] not in {'PreTransaction', 'PostTransaction'} or len(action['Exec']) != 1:
        raise ValueError(f'{path}: invalid Action')
    for trigger in triggers:
        if len(trigger.get('Type', [])) != 1 or trigger['Type'][0] not in {'Package', 'Path', 'File'} or not trigger.get('Target') or not trigger.get('Operation'):
            raise ValueError(f'{path}: incomplete Trigger')
        if any(op not in {'Install', 'Upgrade', 'Remove'} for op in trigger['Operation']):
            raise ValueError(f'{path}: invalid operation')
    return triggers, action


def hook_targets(triggers, records):
    matched = set()
    for trigger in triggers:
        kind = trigger['Type'][0].replace('File', 'Path')
        for record in records:
            if record[0] != kind or record[1] not in trigger['Operation']:
                continue
            value = record[2]
            selected = False
            for pattern in trigger['Target']:
                negative = pattern.startswith('!')
                if fnmatch.fnmatchcase(value, pattern[1:] if negative else pattern):
                    selected = not negative
            if selected:
                matched.add(value)
    return sorted(matched)


def hooks(args):
    records = [line.split('\t') for line in Path(args.manifest).read_text().splitlines()]
    if any(len(row) < 3 for row in records):
        raise ValueError('invalid transaction manifest')
    candidates = {}
    for directory in ('usr/share/libalpm/hooks', 'etc/pacman.d/hooks'):
        for hook in target_path(args.root, directory).glob('*.hook'):
            candidates[hook.name] = hook
    known = installed_names(args.root, args.db_path)
    for name, hook in sorted(candidates.items()):
        # Higher-priority /dev/null symlinks disable a packaged hook.
        if hook.is_symlink() and os.readlink(hook) == '/dev/null':
            continue
        hook = target_path(args.root, str(hook.relative_to(args.root)))
        triggers, action = read_hook(hook)
        targets = hook_targets(triggers, records)
        if action['When'][0] != args.phase or not targets:
            continue
        dependencies = action.get('Depends', [])
        missing = [name for name in dependencies if name not in known]
        if missing:
            print(f'error: hook {name} has missing Depends: {missing}', file=sys.stderr)
            rc = 1
        else:
            command = shlex.split(action['Exec'][0])
            if not command or not command[0].startswith('/'):
                raise ValueError(f'{name}: Exec must name an absolute target executable')
            print(f':: hook {name}: {action.get("Description", [""])[0]}', flush=True)
            rc = in_target(args.root, command, '\n'.join(targets) + '\n' if 'NeedsTargets' in action else '')
        if rc:
            print(f'error: hook {name} failed ({rc})', file=sys.stderr)
            if args.phase == 'PreTransaction' and 'AbortOnFail' in action:
                return 1
    # Only pre-transaction hooks marked AbortOnFail can cancel a transaction.
    # Post-transaction hook failures are reported after packages are committed
    # and cannot roll back the installation.
    return 0


def regenerate_caches(root):
    commands = [
        ('usr/bin/ldconfig', ['/usr/bin/ldconfig']),
        ('usr/bin/update-mime-database', ['/usr/bin/update-mime-database', '/usr/share/mime']),
        ('usr/bin/fc-cache', ['/usr/bin/fc-cache', '-f']),
        ('usr/bin/glib-compile-schemas', ['/usr/bin/glib-compile-schemas', '/usr/share/glib-2.0/schemas']),
        ('usr/bin/gio-querymodules', ['/usr/bin/gio-querymodules', '/usr/lib/gio/modules']),
        ('usr/bin/gdk-pixbuf-query-loaders', ['/usr/bin/gdk-pixbuf-query-loaders', '--update-cache']),
        ('usr/bin/gtk-query-immodules-3.0', ['/usr/bin/gtk-query-immodules-3.0', '--update-cache']),
        ('usr/bin/update-desktop-database', ['/usr/bin/update-desktop-database', '/usr/share/applications']),
    ]
    failed = []
    for executable, command in commands:
        if target_path(root, executable).is_file():
            # Tools for optional desktop data run only when their data is present.
            if executable in {'usr/bin/update-mime-database', 'usr/bin/glib-compile-schemas', 'usr/bin/gio-querymodules', 'usr/bin/update-desktop-database'} and not target_path(root, command[-1]).is_dir():
                continue
            if in_target(root, command):
                failed.append(executable)
    if target_path(root, 'usr/bin/gtk-update-icon-cache').is_file():
        for theme in sorted(target_path(root, 'usr/share/icons').glob('*')):
            if (theme / 'index.theme').is_file():
                command = ['/usr/bin/gtk-update-icon-cache', '-f', '-t', '/usr/share/icons/' + theme.name]
                if in_target(root, command):
                    failed.append(str(theme))
    if failed:
        raise RuntimeError('cache generation failed: ' + ', '.join(failed))


def map_names(names):
    for name in names:
        if not re.fullmatch(r'[A-Za-z0-9@_+][A-Za-z0-9@._+-]*', name):
            raise ValueError(f'invalid package name: {name}')
    result = subprocess.run(['bash', '-c', '. "$1"; shift; for name; do arch_name "$name"; done',
                             'axon-map', str(PROFILE / 'arch-pkgs.sh'), *names], check=True, capture_output=True, text=True)
    mapped = result.stdout.splitlines()
    if len(mapped) != len(names):
        raise RuntimeError('package map returned the wrong number of names')
    return mapped


def profile_source(source):
    if not source:
        raise ValueError('--source must name an AvoryOS source tree for the desktop profile')
    source = Path(source).resolve(strict=True)
    needed = ['userland/avory-account', 'userland/test_accounts.sh', 'userland/avory-login.elf',
              'config/vlc/vlc.sh', 'config/xorg/10-modesetting.conf', 'config/xorg/10-native.conf',
              'config/xorg/10-nouveau.conf', 'config/xorg/20-output.conf', 'config/xorg/44-nouveau-env.sh',
              'config/xorg/45-prime-output.sh', 'config/xorg/avory-nouveau-xorg.sh',
              'config/xorg/avory-prime-display.sh', 'config/xorg/avory-xorg-profile.sh']
    missing = [name for name in needed if not (source / name).is_file()]
    if missing:
        raise ValueError('missing AvoryOS assets: ' + ', '.join(missing))
    return source


def configure(args):
    if args.profile == 'desktop':
        source = profile_source(args.source)
        subprocess.run(['bash', str(PROFILE / 'configure.sh'), str(source), str(args.root)], check=True)
    else:
        # Minimal profile keeps package-provided authentication and account files.
        for relative in ('etc', 'proc', 'sys', 'dev', 'run', 'tmp', 'home'):
            target_path(args.root, relative).mkdir(parents=True, exist_ok=True)
        target_path(args.root, 'tmp').chmod(0o1777)
        mtab = args.root / 'etc/mtab'
        if not mtab.exists() and not mtab.is_symlink():
            mtab.symlink_to('/proc/self/mounts')
    if not args.no_caches:
        regenerate_caches(args.root)
    return 0


def package_file(path):
    return [line.split('#', 1)[0].strip() for line in Path(path).read_text().splitlines()
            if line.split('#', 1)[0].strip()]


def rootfs(args):
    if args.profile == 'desktop':
        profile_source(args.source)  # Fail before installing anything if assets are absent.
    command = [args.axon_executable, '--root', str(args.root), '--db-path', args.db_path,
               '--cache-dir', args.cache_dir, '--sync-dir', args.sync_dir]
    mirrorlist = Path(args.sync_dir) / 'rootfs-mirrors'
    mirrorlist.parent.mkdir(parents=True, exist_ok=True)
    mirrorlist.write_text('Server = https://geo.mirror.pkgbuild.com/$repo/os/$arch\n')
    command += ['--mirrorlist', str(mirrorlist)]
    if args.sync:
        subprocess.run([*command, 'upd'], check=True)
    bootstrap = ['filesystem', 'glibc', 'bash', 'busybox', 'coreutils', 'sed', 'grep', 'gawk',
                 'tar', 'gzip', 'xz', 'util-linux', 'shadow']
    # Empty roots cannot run shell scriptlets. Make that bootstrap exception explicit.
    subprocess.run([*command, 'get', '--needed', '--noconfirm', '--keep-cache',
                    '--no-scriptlets', '--no-hooks', *bootstrap], check=True)
    subprocess.run([*command, 'replay-post-install', *bootstrap], check=True)
    packages = []
    if args.profile == 'desktop':
        legacy = package_file(PROFILE / 'desktop.packages')
        mapped = map_names([p.lstrip('?') for p in legacy])
        packages += [('?' if p.startswith('?') else '') + m for p, m in zip(legacy, mapped) if m != 'DROP']
    for path in args.packages_from:
        names = package_file(path)
        if args.package_names == 'alpine':
            mapped = map_names([name.lstrip('?') for name in names])
            names = [('?' if n.startswith('?') else '') + m for n, m in zip(names, mapped) if m != 'DROP']
        packages += names
    required = list(dict.fromkeys(p for p in packages if not p.startswith('?')))
    optional = list(dict.fromkeys(p[1:] for p in packages if p.startswith('?') and p[1:] not in required))
    for group in (required,):
        if group:
            subprocess.run([*command, 'get', '--needed', '--noconfirm', '--keep-cache',
                            '--assume-installed', 'ntsync-autoload', *group], check=True)
    skipped = []
    for name in optional:
        result = subprocess.run([*command, 'get', '--needed', '--noconfirm', '--keep-cache',
                                 '--assume-installed', 'ntsync-autoload', name])
        if result.returncode:
            skipped.append(name)
    if skipped:
        print('Optional packages not installed:', ', '.join(skipped), file=sys.stderr)
    configure(args)
    if args.output:
        create_image(args)
    return 0


def image_size(text):
    match = re.fullmatch(r'([1-9][0-9]*)([KMGTP]?)', text.upper())
    if not match:
        raise argparse.ArgumentTypeError('size must be a positive byte count or e.g. 512M, 8G')
    size = int(match[1]) * 1024 ** ('KMGTP'.index(match[2]) + 1 if match[2] else 0)
    if size < 16 * 1024 * 1024:
        raise argparse.ArgumentTypeError('image must be at least 16M')
    return size


def restore_image_ownership(root, image):
    commands = []

    def assign(relative, uid, gid, recursive=True):
        host = target_path(root, relative)
        if not host.exists():
            return
        paths = [host]
        if recursive and host.is_dir():
            paths += list(host.rglob('*'))
        for path in paths:
            name = '/' + str(path.relative_to(root))
            if any(character in name for character in '\n\r'):
                raise ValueError('image paths cannot contain newlines')
            quoted = '"' + name.replace('\\', '\\\\').replace('"', '\\"') + '"'
            commands.extend([f'set_inode_field {quoted} uid {uid}', f'set_inode_field {quoted} gid {gid}'])

    passwd = target_path(root, 'etc/passwd')
    if passwd.is_file():
        for line in passwd.read_text().splitlines():
            fields = line.split(':')
            if len(fields) != 7 or not fields[2].isdigit() or not fields[3].isdigit():
                continue
            name, uid, gid, home = fields[0], int(fields[2]), int(fields[3]), fields[5]
            if uid and home.startswith(('/home/', '/var/lib/')):
                assign(home, uid, gid)
            if name == 'sddm':
                for directory in ('var/log/sddm', 'run/sddm'):
                    assign(directory, uid, gid)
    group = target_path(root, 'etc/group')
    if group.is_file():
        for line in group.read_text().splitlines():
            fields = line.split(':')
            if len(fields) >= 3 and fields[0] == 'polkitd' and fields[2].isdigit():
                assign('etc/polkit-1/rules.d', 0, int(fields[2]), recursive=False)
    if commands:
        with tempfile.NamedTemporaryFile(mode='w', prefix='axon-image-ownership-') as batch:
            batch.write('\n'.join(commands) + '\n'); batch.flush()
            result = subprocess.run(['debugfs', '-w', '-f', batch.name, str(image)],
                                    text=True, capture_output=True, check=True)
            # debugfs can return zero even when an individual command failed.
            if any(message in result.stderr for message in ('not found', 'File not found', 'Usage:', 'Invalid')):
                raise RuntimeError('could not apply target image ownership: ' + result.stderr)


def image_tree(root, destination, include_cache):
    def link_or_copy(source, target):
        try:
            os.link(source, target)
        except OSError:
            shutil.copy2(source, target)
        return target

    def ignore(directory, names):
        relative = Path(directory).relative_to(root)
        if not include_cache and str(relative) in {'var/cache/axon/pkg', 'var/lib/axon/sync'}:
            return names
        return []

    shutil.copytree(root, destination, symlinks=True, copy_function=link_or_copy, ignore=ignore)


def create_image(args):
    if not args.output:
        raise ValueError('--output is required for image creation')
    destination = Path(args.output).absolute()
    if destination.exists() and not args.force:
        raise ValueError(f'output exists: {destination}; use --force to replace a regular image')
    if destination.is_symlink() or (destination.exists() and not destination.is_file()):
        raise ValueError('output must be a regular file, never a symlink or device')
    parent = destination.parent.resolve(strict=True)
    if parent == args.root or args.root in parent.parents:
        raise ValueError('image output must be outside the source rootfs')
    for program in ('mke2fs', 'e2fsck', 'debugfs'):
        if not shutil.which(program):
            raise ValueError(f'image creation requires {program} (e2fsprogs)')
    fd, temporary = tempfile.mkstemp(prefix='.axon-image-', dir=parent)
    staging = None
    try:
        os.ftruncate(fd, args.size)
        os.close(fd)
        staging = tempfile.TemporaryDirectory(prefix='axon-image-tree-', dir=parent)
        image_source = Path(staging.name) / 'root'
        image_tree(args.root, image_source, args.include_cache)
        format_command = ['mke2fs', '-q', '-F', '-t', args.filesystem, '-L', 'AvoryOS',
                          '-E', 'root_owner=0:0', '-d', str(image_source), temporary]
        # Rootless extraction leaves host-owned files; view them as uid 0 while
        # copying into the image, matching setup-arch's user-namespace install.
        if os.geteuid() != 0:
            format_command = ['unshare', '-rm', *format_command]
        subprocess.run(format_command, check=True)
        restore_image_ownership(args.root, temporary)
        subprocess.run(['e2fsck', '-fn', temporary], check=True)
        if args.force:
            os.replace(temporary, destination)
        else:
            os.link(temporary, destination)  # No race that can replace an existing file.
            os.unlink(temporary)
        print(f'Created {args.filesystem} filesystem image: {destination}')
    finally:
        if staging is not None:
            staging.cleanup()
        if os.path.exists(temporary):
            os.unlink(temporary)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='operation', required=True)
    for name in ('hooks', 'scriptlet', 'target', 'cache', 'map', 'configure', 'rootfs', 'image'):
        command = commands.add_parser(name)
        command.add_argument('--root', type=Path, required=True)
        command.add_argument('--db-path', required=True)
        command.add_argument('--cache-dir', default='/var/cache/axon/pkg')
        command.add_argument('--sync-dir', default='/var/lib/axon/sync')
        command.add_argument('--axon-executable', default='axon')
        if name == 'scriptlet':
            command.add_argument('--phase', choices=['pre_install', 'post_install'], required=True)
            command.add_argument('--version', required=True)
        if name == 'target':
            command.add_argument('command', nargs=argparse.REMAINDER)
        if name == 'hooks':
            command.add_argument('--phase', choices=['PreTransaction', 'PostTransaction'], required=True)
            command.add_argument('--manifest', required=True)
        if name == 'map':
            command.add_argument('names', nargs='+')
        if name in ('rootfs', 'configure'):
            command.add_argument('--profile', choices=['minimal', 'desktop'], default='minimal')
            command.add_argument('--source')
            command.add_argument('--no-caches', action='store_true')
        if name == 'rootfs':
            command.add_argument('--sync', action='store_true')
            command.add_argument('--packages-from', action='append', default=[])
            command.add_argument('--package-names', choices=['arch', 'alpine'], default='arch')
        if name in ('rootfs', 'image'):
            command.add_argument('--output')
            command.add_argument('--size', type=image_size, default=image_size('4G'))
            command.add_argument('--filesystem', choices=['ext2', 'ext3', 'ext4'], default='ext2')
            command.add_argument('--force', action='store_true')
            command.add_argument('--include-cache', action='store_true')
    args = parser.parse_args(argv)
    args.root = args.root.resolve(strict=True)
    if not args.root.is_dir():
        raise ValueError('target root must be a directory')
    if args.operation in {'configure', 'rootfs', 'image'} and args.root == Path('/'):
        raise ValueError('rootfs/configure/image require an alternate --root')
    if args.operation == 'scriptlet':
        return in_target(args.root, ['/bin/bash', '-s', '--', args.phase, args.version], sys.stdin.read())
    if args.operation == 'target':
        command = args.command[1:] if args.command[:1] == ['--'] else args.command
        if not command:
            raise ValueError('target command required')
        return in_target(args.root, command)
    if args.operation == 'hooks':
        return hooks(args)
    if args.operation == 'cache':
        regenerate_caches(args.root); return 0
    if args.operation == 'map':
        print('\n'.join(map_names(args.names))); return 0
    if args.operation == 'configure':
        return configure(args)
    if args.operation == 'rootfs':
        return rootfs(args)
    return create_image(args)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, RuntimeError, OSError, subprocess.SubprocessError) as error:
        print(f'error: {error}', file=sys.stderr)
        sys.exit(1)
