# Root filesystem and installation support

Axon can install packages into a separate root, execute installation scripts and
ALPM hooks there, apply the AvoryOS desktop profile, regenerate caches, and build
an ext2/ext3/ext4 filesystem image. The root directory must already exist.

## Build a rootfs

```sh
mkdir -p /tmp/avory-root
./axon rootfs --root /tmp/avory-root --sync \
    --profile desktop --source /home/offihito/AvoryOS
./axon image --root /tmp/avory-root --output /tmp/avory-root.img --size 16G
```

Alternatively, append `--output /tmp/avory-root.img --size 16G` to the rootfs
command. `--profile minimal` is the default: install the shell/runtime/core
utilities, create runtime directories and preserve package-provided accounts and
PAM. Add packages with repeatable `--packages-from FILE`. Each line contains a
package, optionally preceded by `?` for a best-effort package; `#` starts a comment.
Use `--package-names alpine` to translate these files' old Alpine names.

Rootfs uses the upstream Arch x86_64 mirror explicitly, rather than host-tuned
repositories. `--sync` refreshes repositories first; without it the target must
already have sync databases. The native Axon package resolver/downloader is used,
not host pacman. Installed packages are skipped. Existing incompatible versions
cause a clear error; this command is not a rolling-release upgrade engine.

For an empty root, the first package set installs with scripts and hooks disabled
because the target initially lacks bash and libc. Once those packages are present,
`replay-post-install` runs their post_install functions from matching cached
archives and the post-transaction hooks. Pre-install functions and pre-transaction
hooks for that first bootstrap set are deliberately not replayed after extraction.
All following package installs use normal scripts/hooks. Archives remain cached
for replay. Replay requires repository metadata to match the installed versions.

## Existing roots and package names

```sh
./axon configure --root /tmp/avory-root --profile desktop --source /home/offihito/AvoryOS
./axon cache --root /tmp/avory-root
./axon map qt6-qtbase libpng-dev openrc
```

`map` prints one result per input (`qt6-base`, `DROP`, `DROP` for this example).
The map and desktop package list are snapshots migrated from AvoryOS on
2026-10-23. Normal `get` continues to use canonical package names.

The desktop profile migrates the setup script's accounts, login helpers, session
wrappers, BusyBox boot scripts, Xorg profiles, SDDM/PAM, XDG defaults, GTK/XFCE,
Plasma, MIME/font/GSettings/module/icon caches and target pacman configuration.
It intentionally preserves AvoryOS's existing development login policy, including
passwordless root/avory accounts and the existing permissive SDDM PAM settings.
It is intended for disposable AvoryOS image roots, not host system administration.

The desktop profile needs the AvoryOS source tree's `config/xorg`, VLC wrapper,
`userland/avory-account`, `userland/test_accounts.sh` and built `avory-login.elf`.
It no longer calls `setup-arch.sh` or the source tree's account script. Assets are
checked before configuration starts. The existing VLC launcher patch remains
hash-pinned; newer binaries require updating that profile patch. Qt's XCB loader
check is retained and fails if target dependencies are incomplete.

`--no-caches` skips the final explicit cache pass. The migrated desktop profile
also retains its own setup-time cache operations. `cache` reports failures for
available tools instead of claiming success; absent optional tools are skipped.

## Installation lifecycle

`get` sources `.INSTALL` in target bash and calls `pre_install VERSION` before
extraction and `post_install VERSION` after database recording. Nonzero exit status
stops the request and returns failure. Rootless target execution uses Linux user
and mount namespaces plus chroot; only `/dev/null`, `/dev/zero`, `/dev/random` and
`/dev/urandom` are temporarily bound into the target. Host execution is never used
as a fallback for an alternate root. Target programs must match the host CPU and
have their runtime libraries installed.

Install transactions read `/usr/share/libalpm/hooks` and `/etc/pacman.d/hooks` in
the target. Custom files override packaged files by name; an override symlink to
`/dev/null` disables a hook. Hooks run alphabetically. Supported fields include
repeatable Trigger sections, Operation, Package/Path (or File) Type, globbed and
negated Target, Description, When, quoted Exec arguments, Depends, NeedsTargets
and AbortOnFail. Depends is a package name. Pre hooks use the pre-extraction path
manifest; post hooks reuse it and discover newly installed hook files. Existing
paths are Upgrade path events, while newly installed packages are Install package
events. A failing AbortOnFail pre hook prevents extraction. Other failing pre
hooks warn and continue. Post hook failures return an error after installation.

This hook integration covers `get` installs and bootstrap post-install replay;
`rm` and `upg` have not been converted to this lifecycle. `upg` currently reports
available updates without installing them. The file extraction layer does not
provide automatic transaction rollback; a script/extraction/post-hook failure
may leave already installed files. `--no-scriptlets` and `--no-hooks` explicitly
skip their respective lifecycle stages.

Dependency validation checks `=`, `<`, `<=`, `>` and `>=`, including epoch/pkgrel
comparison and versioned `provides`. An unversioned provide does not satisfy a
versioned dependency. Both Axon and target pacman databases are consulted. Cyclic
requirements must agree on versions. Missing or incompatible dependencies abort
before download/extraction; an installed incompatible package requires an upgrade.
`--assume-installed NAME` remains an explicit bare-name bootstrap exception.

Hook syntax reference: [Arch ALPM hook manual](https://man.archlinux.org/man/alpm-hooks.5.en).

## Image creation

`image` creates a fresh filesystem with `mke2fs -d` and checks it with `e2fsck`.
Rootless source ownership is mapped to root, then home/service account ownership
and the polkit rules directory group are restored from the target account files.
Downloaded archives and sync caches are excluded by default; `--include-cache`
keeps them. Installed-package metadata is retained. The source tree is preserved.

The image is published only after filesystem validation. Existing output requires
`--force`; symlink/device outputs and output paths inside the source tree are
rejected. Image failure preserves existing output. This creates a filesystem
image, not a bootable ISO or a partition table/bootloader. Kernel/bootloader
packaging remains part of the AvoryOS build.

Host requirements: Python 3, Bash, util-linux (`unshare`, `mount`, `chroot`), and
for images e2fsprogs (`mke2fs`, `e2fsck`, `debugfs`). Existing native build
requirements remain libarchive and libcurl. `make install` installs the runtime
helper and profile alongside the binary under PREFIX/libexec/axon.

## Validation

`make test` includes the C dependency-version/provides checks and offline Python
integration tests. These run real scriptlets, hooks and cache commands inside a
temporary root, apply the desktop configuration twice to a fixture, replay a
bootstrap script, and create/check/read an actual ext2 image with ownership checks.
They do not download the complete desktop package set or boot an AvoryOS desktop.
