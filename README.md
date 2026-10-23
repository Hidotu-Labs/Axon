# Axon Package Manager

**Axon** is a lightweight, independent package manager built from scratch for AvoryOS.

Axon prioritizes official AvoryOS repositories and falls back to Arch Linux repositories when a package is unavailable.

## Usage

```bash
axon get firefox
axon rm firefox
axon look firefox
axon upd
axon upg
```

## Credits

Special thanks to the **Arch Linux** project and its contributors for providing the package repositories and infrastructure used as a fallback by Axon.

Arch Linux: https://archlinux.org/

Axon is an independent project and is not affiliated with or endorsed by Arch Linux.

## License

See the LICENSE file for details.

## Building a target root filesystem

The target directory must exist. State options can appear before or after the
operation. `--root` resolves symlinks and puts the Axon database, archive cache,
sync database and pacman fallback database under that root. Explicit
`--db-path`, `--cache-dir` and `--sync-dir` paths override those defaults,
regardless of option order. The mirrorlist is still host configuration.

```sh
mkdir -p /tmp/avory-root
./axon --root /tmp/avory-root upd
./axon get --root /tmp/avory-root --needed --noconfirm --keep-cache \
    --assume-installed ntsync-autoload bash coreutils tcc
```

`--needed` documents the existing behavior: installed targets and dependencies
are skipped. `--assume-installed` accepts a bare dependency name and may be
repeated; it neither installs that dependency nor writes a fake database entry.
Versioned exceptions are rejected. Any target resolution failure aborts the
whole request before downloads or extraction. This does not provide rollback
for extraction failures.

Installation scripts, install hooks, versioned dependencies, the migrated AvoryOS
desktop profile, cache regeneration and filesystem image creation are now
available. See [rootfs and installation support](docs/rootfs.md) for commands,
host requirements, bootstrap behavior and current limitations.

Run `make test-bootstrap` for offline target-root regression checks, or
`make test` for all tests, including target execution and real image creation.
