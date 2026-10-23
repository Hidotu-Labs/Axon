#!/usr/bin/env bash
# AvoryOS desktop profile, migrated from setup-arch.sh on 2026-10-23.
set -euo pipefail
ROOT_DIR=${1:?AvoryOS source tree required}
ROOTFS_DIR=${2:?Target root required}
PROFILE_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "${ROOTFS_DIR}"/{usr/bin,usr/sbin,sbin,var/lib,etc/pacman.d}

# User accounts, DRI symlinks, session wrappers (startplasma-x11, kwin).
# configure-accounts.sh is package-manager agnostic (operates on files).
echo "[*] Configuring accounts and session wrappers..."
AVORY_SOURCE="${ROOT_DIR}" bash "${PROFILE_DIR}/configure-accounts.sh" "${ROOTFS_DIR}"

# VLC: keep the packaged launcher, bypass its root refusal, and install
# the shared CLI/desktop wrapper with an explicit plugin search path.
if [ -f "${ROOTFS_DIR}/usr/bin/vlc" ]; then
    (
        vlc_dir="${ROOTFS_DIR}/usr/bin"
        vlc_wrapper="${ROOT_DIR}/config/vlc/vlc.sh"
        vlc_source="${vlc_dir}/vlc"
        if cmp -s "${vlc_source}" "${vlc_wrapper}"; then
            vlc_source="${vlc_dir}/vlc.distrib"
        fi
        # This exact Arch launcher tests geteuid() and branches to its
        # root refusal at 0x108d. Reject other builds before modifying files.
        vlc_digest=$(sha256sum "${vlc_source}")
        if [ "${vlc_digest%% *}" != "8e08d98bb6107ab211e87fdad3cee7dc5dc37497a58529a77ab14d263d9bd521" ]; then
            echo "Unrecognized VLC launcher; inspect its root check before updating this patch" >&2
            exit 1
        fi
        if [ "${vlc_source}" != "${vlc_dir}/vlc.distrib" ]; then
            install -m 0755 "${vlc_source}" "${vlc_dir}/vlc.distrib"
        fi
        install -m 0755 "${vlc_dir}/vlc.distrib" "${vlc_dir}/vlc.bin"
        printf '\220\220\220\220\220\220' | dd of="${vlc_dir}/vlc.bin" bs=1 seek=4237 conv=notrunc status=none
        install -m 0755 "${vlc_wrapper}" "${vlc_dir}/vlc"
        echo "[*] Installed VLC wrapper, plugin path, and root-capable launcher"
    )
fi

# --- Minimal boot contract (busybox init, no OpenRC on Arch) ---
# /sbin/init is busybox; it runs /etc/init.d/rcS once, then respawns the
# console login. No service manager, no dependency graph to satisfy.
mkdir -p "${ROOTFS_DIR}/etc/init.d"
cat <<'EOF' > "${ROOTFS_DIR}/etc/init.d/rcS"
#!/bin/sh
# AvoryOS boot (busybox init rcS; OpenRC is unavailable on Arch official
# repos, so this script is the service manager: fsck -> dbus -> udev ->
# NetworkManager -> pipewire,
# each guarded so a missing binary or empty config never fails the boot.
# Status lines mimic OpenRC's ` * Starting foo ... [ ok ]` format (plain
# text, no ANSI colors, so the serial log stays clean).
ebegin() { printf ' * %s ...' "$*"; }
eend() { if [ "${1:-0}" -eq 0 ] 2>/dev/null; then printf ' [ ok ]\n'; else printf ' [ !! ]\n'; fi; return 0; }
eskip() { printf ' [ skipped ]\n'; return 0; }

ebegin "Mounting pseudo-filesystems"
mount -t proc proc /proc 2>/dev/null; rc1=$?
mount -t sysfs sysfs /sys 2>/dev/null; rc2=$?
mount -t devtmpfs dev /dev 2>/dev/null || true
mkdir -p /run /run/lock /run/mount /var/log /tmp /dev/shm 2>/dev/null
chmod 1777 /tmp /dev/shm 2>/dev/null || true
mount -t tmpfs -o rw,nosuid,nodev,mode=1777 tmpfs /dev/shm 2>/dev/null || true
if [ "$rc1" -eq 0 ] && [ "$rc2" -eq 0 ]; then eend 0; else eend 1; fi

ebegin "Setting hostname"
/usr/bin/busybox hostname AvoryOS 2>/dev/null; eend $?

if [ -x /sbin/avory-early-init ]; then
    ebegin "Running early init"
    /sbin/avory-early-init 2>/dev/null; eend $?
fi
if [ ! -s /etc/machine-id ]; then
    echo "b08040a9e7114fea9634b7f94da7e722" > /etc/machine-id
fi
ebegin "Checking filesystems"
if [ -x /sbin/fsck ] && grep -q -v '^[[:space:]]*\(#\|$\)' /etc/fstab 2>/dev/null; then
    fsck -A -T -a >>/var/log/boot.log 2>&1; eend $?
else
    eskip
fi
mkdir -p /var/lib/dbus /run/dbus
[ -e /run/mount/utab ] || : > /run/mount/utab 2>/dev/null || true
[ -e /var/lib/dbus/machine-id ] || cp -f /etc/machine-id /var/lib/dbus/machine-id 2>/dev/null || true
command -v dbus-uuidgen >/dev/null 2>&1 && dbus-uuidgen --ensure 2>/dev/null || true
ebegin "Starting system message bus"
if command -v dbus-daemon >/dev/null 2>&1 && [ ! -S /run/dbus/system_bus_socket ]; then
    dbus-daemon --system --fork 2>/dev/null || true
fi
# Wait for the system bus so dependents (pipewire, logins) do not race it.
for i in 1 2 3 4 5 6 7 8 9 10; do
    [ -S /run/dbus/system_bus_socket ] && break
    sleep 0.5
done
if [ -S /run/dbus/system_bus_socket ]; then eend 0; else eend 1; fi
# udev processes coldplug events before audio discovers hardware. It runs
# independently of systemd PID 1; BusyBox init remains the service manager.
ebegin "Starting device manager"
UDEV_OK=1
if [ -x /usr/lib/systemd/systemd-udevd ] && command -v udevadm >/dev/null 2>&1; then
    mkdir -p /run/udev
    /usr/lib/systemd/systemd-udevd --daemon --resolve-names=never >>/var/log/udev.log 2>&1
    if udevadm control --timeout=5 --ping >>/var/log/udev.log 2>&1; then
        UDEV_OK=0
    fi
fi
eend "$UDEV_OK"
if [ "$UDEV_OK" -eq 0 ]; then
    ebegin "Enumerating devices"
    if udevadm trigger --type=subsystems --action=add >>/var/log/udev.log 2>&1 &&
       udevadm trigger --type=devices --action=add >>/var/log/udev.log 2>&1; then
        eend 0
    else
        eend 1
    fi
    ebegin "Waiting for device events"
    udevadm settle --timeout=10 >>/var/log/udev.log 2>&1; eend $?
fi

# NetworkManager is a system service; start it once after D-Bus and coldplug.
ebegin "Starting NetworkManager"
NM_OK=1
if command -v NetworkManager >/dev/null 2>&1; then
    mkdir -p /run/NetworkManager /var/lib/NetworkManager
    if [ -S /run/dbus/system_bus_socket ]; then
        if ! pidof NetworkManager >/dev/null 2>&1; then
            nohup NetworkManager --no-daemon >>/var/log/NetworkManager.log 2>&1 &
        fi
        for i in 1 2 3 4 5 6 7 8 9 10; do
            if LC_ALL=C nmcli --wait 1 -t -f RUNNING general 2>/dev/null | grep -qx running; then
                NM_OK=0
                break
            fi
            sleep 0.5
        done
    fi
    eend "$NM_OK"
else
    eskip
fi
unset NM_OK

# --- pipewire system instance (root-owned; user sessions start their own
# copy in the startplasma-x11 wrapper if none is listening) ---
ebegin "Starting PipeWire"
PIPE_OK=1
if command -v pipewire >/dev/null 2>&1; then
    PIPE_RUNTIME=/run/pipewire
    mkdir -p "$PIPE_RUNTIME" 2>/dev/null || true
    chmod 0755 "$PIPE_RUNTIME" 2>/dev/null || true
    if [ ! -S "$PIPE_RUNTIME/pipewire-0" ]; then
        XDG_RUNTIME_DIR="$PIPE_RUNTIME" nohup pipewire >>/var/log/pipewire.log 2>&1 &
        for i in 1 2 3 4 5 6 7 8 9 10; do
            [ -S "$PIPE_RUNTIME/pipewire-0" ] && break
            sleep 0.5
        done
    fi
    [ -S "$PIPE_RUNTIME/pipewire-0" ] && PIPE_OK=0
    eend "$PIPE_OK"
    if [ "$PIPE_OK" -eq 0 ]; then
        ebegin "Starting WirePlumber"
        if command -v wireplumber >/dev/null 2>&1; then
            if command -v pidof >/dev/null 2>&1; then
                # Keep the probe synchronous; background only the daemon,
                # not an extra shell wrapping the entire `||` list.
                if ! pidof wireplumber >/dev/null 2>&1; then
                    XDG_RUNTIME_DIR="$PIPE_RUNTIME" nohup wireplumber >>/var/log/pipewire.log 2>&1 &
                fi
            else
                XDG_RUNTIME_DIR="$PIPE_RUNTIME" nohup wireplumber >>/var/log/pipewire.log 2>&1 &
            fi
            eend 0
        else
            eskip
        fi
        ebegin "Starting PipeWire PulseAudio bridge"
        if command -v pipewire-pulse >/dev/null 2>&1; then
            if [ ! -S "$PIPE_RUNTIME/pulse/native" ]; then
                XDG_RUNTIME_DIR="$PIPE_RUNTIME" nohup pipewire-pulse >>/var/log/pipewire.log 2>&1 &
            fi
            for i in 1 2 3 4 5 6 7 8 9 10; do
                [ -S "$PIPE_RUNTIME/pulse/native" ] && break
                sleep 0.5
            done
            if [ -S "$PIPE_RUNTIME/pulse/native" ]; then eend 0; else eend 1; fi
        else
            eskip
        fi
    fi
    unset PIPE_RUNTIME
else
    eskip
fi
echo " * Entering runlevel: default"
EOF
chmod +x "${ROOTFS_DIR}/etc/init.d/rcS"

# Early init selects the Xorg profile; udev owns the device database.
cat <<'EOF' > "${ROOTFS_DIR}/sbin/avory-early-init"
#!/bin/sh
mkdir -p /run/plymouth /var/log

# Select the Xorg profile for this boot's GPU topology before any display
# manager runs: an image baked for VFIO otherwise leaves Xorg pointed at a
# nonexistent card2 on plain `make run` boots and the server dies instantly.
if [ -x /usr/bin/avory-xorg-profile ]; then
    /usr/bin/avory-xorg-profile
fi
EOF
chmod +x "${ROOTFS_DIR}/sbin/avory-early-init"

# Console login binary (inittab respawns it on tty1).
if [ -f "${ROOT_DIR}/userland/avory-login.elf" ]; then
    cp "${ROOT_DIR}/userland/avory-login.elf" "${ROOTFS_DIR}/bin/avory-login"
    chmod +x "${ROOTFS_DIR}/bin/avory-login"
fi

# Both Xorg profiles ship in the image; /sbin/avory-early-init selects the
# one matching the boot's PCI topology (amdgpu passthrough vs native
# card0), so bare `sddm` works with or without VFIO. The default copy is
# the safe native layout; the disk-image step reapplies the build-time
# XORG_PROFILE choice for boots where detection is inconclusive.
mkdir -p "${ROOTFS_DIR}/etc/X11/xorg.conf.d" "${ROOTFS_DIR}/usr/share/avory-xorg"
install -m 0644 "${ROOT_DIR}/config/xorg/10-modesetting.conf" \
    "${ROOTFS_DIR}/usr/share/avory-xorg/10-modesetting-amdgpu.conf"
install -m 0644 "${ROOT_DIR}/config/xorg/10-native.conf" \
    "${ROOTFS_DIR}/usr/share/avory-xorg/10-modesetting-native.conf"
install -m 0644 "${ROOT_DIR}/config/xorg/10-native.conf" \
    "${ROOTFS_DIR}/etc/X11/xorg.conf.d/10-modesetting.conf"
install -m 0755 "${ROOT_DIR}/config/xorg/avory-xorg-profile.sh" \
    "${ROOTFS_DIR}/usr/bin/avory-xorg-profile"
install -m 0755 "${ROOT_DIR}/config/xorg/avory-nouveau-xorg.sh" \
    "${ROOTFS_DIR}/usr/bin/avory-nouveau-xorg"
install -m 0644 "${ROOT_DIR}/config/xorg/10-nouveau.conf" \
    "${ROOTFS_DIR}/usr/share/avory-xorg/10-modesetting-nouveau.conf"
install -d "${ROOTFS_DIR}/etc/X11/xinit/xinitrc.d" "${ROOTFS_DIR}/etc/profile.d"
install -m 0755 "${ROOT_DIR}/config/xorg/44-nouveau-env.sh" \
    "${ROOTFS_DIR}/etc/X11/xinit/xinitrc.d/44-nouveau-env.sh"
install -m 0644 "${ROOT_DIR}/config/xorg/44-nouveau-env.sh" \
    "${ROOTFS_DIR}/etc/profile.d/avory-nouveau.sh"
# SDDM display manager defaults, baked into the image so bare `sddm`
# starts with a workable root login and output binding without a session
# launcher regenerating them first. Without /etc/sddm.conf SDDM hides
# uid<1000 (no root login);
# without permissive PAM the stock Arch sddm* services can fail the
# greeter session on AvoryOS (no systemd/logind, no shadow).
mkdir -p "${ROOTFS_DIR}/etc/sddm.conf.d" "${ROOTFS_DIR}/etc/pam.d"
install -m 0644 "${ROOT_DIR}/config/xorg/20-output.conf" \
    "${ROOTFS_DIR}/etc/sddm.conf.d/20-output.conf"
install -m 0755 "${ROOT_DIR}/config/xorg/avory-prime-display.sh" \
    "${ROOTFS_DIR}/usr/bin/avory-prime-display"
mkdir -p "${ROOTFS_DIR}/etc/X11/xinit/xinitrc.d"
install -m 0755 "${ROOT_DIR}/config/xorg/45-prime-output.sh" \
    "${ROOTFS_DIR}/etc/X11/xinit/xinitrc.d/45-prime-output.sh"
cat > "${ROOTFS_DIR}/etc/sddm.conf" <<'EOF'
[General]
DisplayServer=x11
HaltCommand=/sbin/poweroff
RebootCommand=/sbin/reboot
Numlock=none

[Theme]
Current=breeze
ThemeDir=/usr/share/sddm/themes
FacesDir=/usr/share/sddm/faces
CursorTheme=Adwaita

[X11]
ServerPath=/usr/bin/Xorg
ServerArguments=-noreset -nolisten tcp -ac
SessionCommand=/usr/share/sddm/scripts/Xsession
SessionDir=/usr/share/xsessions
DisplayCommand=/usr/bin/avory-prime-display
DisplayStopCommand=/usr/share/sddm/scripts/Xstop
XauthPath=/usr/bin/xauth
EnableHiDPI=true

[Users]
DefaultPath=/usr/bin:/bin:/usr/local/bin:/opt/coreutils/bin:/opt/bash/bin
MinimumUid=0
MaximumUid=60000
HideUsers=
RememberLastUser=true
RememberLastSession=true

[Last]
Session=plasmax11.desktop
User=root
EOF
cat > "${ROOTFS_DIR}/etc/pam.d/sddm-greeter" <<'EOF'
#%PAM-1.0
auth      required  pam_env.so
auth      required  pam_permit.so
account   required  pam_permit.so
password  required  pam_deny.so
session   required  pam_unix.so
-session  optional  pam_elogind.so
EOF
cat > "${ROOTFS_DIR}/etc/pam.d/sddm" <<'EOF'
#%PAM-1.0
auth      required  pam_permit.so
account   required  pam_permit.so
password  required  pam_deny.so
session   required  pam_env.so
session   required  pam_unix.so
-session  optional  pam_limits.so
-session  optional  pam_elogind.so
EOF
cat > "${ROOTFS_DIR}/etc/pam.d/sddm-autologin" <<'EOF'
#%PAM-1.0
auth      required  pam_permit.so
account   required  pam_permit.so
password  required  pam_deny.so
session   required  pam_env.so
session   required  pam_unix.so
-session  optional  pam_limits.so
-session  optional  pam_elogind.so
EOF
# Inittab for busybox init: one rcS pass, console login, sane shutdown.
# /sbin/init is busybox (symlinked below; Arch ships no init of its own here).
ln -sf /usr/bin/busybox "${ROOTFS_DIR}/sbin/init" 2>/dev/null || true
cat <<'EOF' > "${ROOTFS_DIR}/etc/inittab"
# /etc/inittab for AvoryOS (busybox init)

::sysinit:/etc/init.d/rcS

# Console login on tty1
tty1::respawn:/bin/avory-login

# Shutdown & reboot
::shutdown:/bin/umount -a -r 2>/dev/null
::ctrlaltdel:/sbin/reboot
EOF
# /etc/mtab -> /proc/self/mounts: df, mount, libmount and Qt's QStorageInfo
# fall back to it when mountinfo is unavailable; without it they report 0 B.
rm -f "${ROOTFS_DIR}/etc/mtab" 2>/dev/null || true
ln -sf /proc/self/mounts "${ROOTFS_DIR}/etc/mtab" 2>/dev/null || true

# gdk-pixbuf loader registry: generated below by gdk-pixbuf-query-loaders
# (without it every themed-icon load fails: xfce4-panel dies at startup
# in ensure_surface_for_gicon with Gtk:ERROR → abort, see serial log).

# Generated databases (same category: built at image time, not patches).
# pacman hooks would do this on a live system; here one native pass covers
# everything. Target binaries are Arch glibc same-arch builds, so they run
# natively under unshare+chroot - no qemu involved.
target_run() {
    python3 "${PROFILE_DIR}/../../axon-runtime.py" target --root "${ROOTFS_DIR}" --db-path "${ROOTFS_DIR}/var/lib/axon" -- "$@"
}

# A Qt platform-plugin abort can leave the Plasma session waiting for a
# D-Bus helper that never registered. Validate XCB and its transitive library
# dependencies in the target namespace before assembling another disk image.
echo "[*] Checking Qt6 X11 platform plugin dependencies..."
if [ ! -f "${ROOTFS_DIR}/usr/lib/qt6/plugins/platforms/libqxcb.so" ]; then
    echo "[!] Missing Qt6 XCB plugin; reinstall qt6-base in the target rootfs." >&2
    exit 1
fi
if ! target_run /usr/lib/ld-linux-x86-64.so.2 --list \
    /usr/lib/qt6/plugins/platforms/libqxcb.so > /dev/null; then
    echo "[!] Qt6 XCB dependencies are incomplete; repair the packages before rebuilding the image." >&2
    exit 1
fi

# PackageKit uses the target's pacman configuration and keyring, not the
# host-side bootstrap configuration (which disables signature checking).
# pacman-mirrorlist ships every server commented out. Keep any configured
# mirrors, but seed the guest's list when none are enabled: otherwise
# PackageKit reports "No more mirrors available" for every installation.
if ! grep -Eq '^[[:space:]]*Server[[:space:]]*=' "${ROOTFS_DIR}/etc/pacman.d/mirrorlist"; then
    echo "[*] Enabling target Arch package mirrors..."
    cat >> "${ROOTFS_DIR}/etc/pacman.d/mirrorlist" <<'EOF'

# AvoryOS defaults for Discover/PackageKit and pacman (HTTPS only).
Server = https://geo.mirror.pkgbuild.com/$repo/os/$arch
Server = https://fastly.mirror.pkgbuild.com/$repo/os/$arch
EOF
fi
# Pacman's downloader otherwise fails on landlock_create_ruleset (ENOSYS).
# AvoryOS has no Landlock implementation; disable only that sandbox layer.
# PackageKit's alpm backend ignores this option and does not set DownloadUser.
if ! grep -Eq '^[[:space:]]*DisableSandboxFilesystem([[:space:]]|$)' "${ROOTFS_DIR}/etc/pacman.conf"; then
    sed -i '/^\[options\]$/a\# AvoryOS does not implement Linux Landlock.\nDisableSandboxFilesystem' \
        "${ROOTFS_DIR}/etc/pacman.conf"
fi
if [ -x "${ROOTFS_DIR}/usr/bin/pacman-key" ]; then
    echo "[*] Initializing target Arch package signing keys..."
    if ! target_run /usr/bin/pacman-key --init || \
       ! target_run /usr/bin/pacman-key --populate archlinux; then
        echo "[!] Target keyring initialization failed; signed package installs need pacman-key --init and --populate archlinux in the guest." >&2
    fi
fi

if [ -d "${ROOTFS_DIR}/usr/share/mime" ] && \
   [ -x "${ROOTFS_DIR}/usr/bin/update-mime-database" ]; then
    echo "[*] Updating MIME database..."
    target_run /usr/bin/update-mime-database /usr/share/mime 2>/dev/null || true
fi

mkdir -p "${ROOTFS_DIR}/var/cache/fontconfig"
if [ -x "${ROOTFS_DIR}/usr/bin/fc-cache" ]; then
    echo "[*] Building font cache..."
    target_run /usr/bin/fc-cache -f 2>/dev/null || true
else
    echo "[!] No fc-cache in target; apps will rebuild font index per launch" >&2
fi

if [ -d "${ROOTFS_DIR}/usr/share/glib-2.0/schemas" ] && \
   [ -x "${ROOTFS_DIR}/usr/bin/glib-compile-schemas" ]; then
    echo "[*] Compiling GSettings schemas..."
    target_run /usr/bin/glib-compile-schemas /usr/share/glib-2.0/schemas 2>/dev/null || true
else
    echo "[!] No glib-compile-schemas in target; GSettings apps will abort at startup" >&2
fi

if [ -d "${ROOTFS_DIR}/usr/lib/gio/modules" ] && \
   [ -x "${ROOTFS_DIR}/usr/bin/gio-querymodules" ]; then
    echo "[*] Caching GIO modules..."
    target_run /usr/bin/gio-querymodules /usr/lib/gio/modules 2>/dev/null || true
fi
if [ -x "${ROOTFS_DIR}/usr/bin/gdk-pixbuf-query-loaders" ]; then
    echo "[*] Caching gdk-pixbuf loaders..."
    target_run /usr/bin/gdk-pixbuf-query-loaders --update-cache 2>/dev/null || true
fi
if [ -x "${ROOTFS_DIR}/usr/bin/gtk-query-immodules-3.0" ]; then
    echo "[*] Caching GTK input-method modules..."
    target_run /usr/bin/gtk-query-immodules-3.0 --update-cache 2>/dev/null || true
fi

# libwnck default_icon_at_size abort fix (xfce4-session SIGABRT, base==NULL):
# libwnck scales the default window icon; when the Gtk icon lookup returns
# NULL (no icon-theme.cache, no SVG/PNG loader, or no fallback theme for
# root with HOME=/) it calls g_error() -> abort(). Rebuild the icon caches
# here (pacman hooks would do it on live Arch) and seed a fallback theme.
if [ -x "${ROOTFS_DIR}/usr/bin/gtk-update-icon-cache" ]; then
    echo "[*] Caching GTK icon themes..."
    for _theme in hicolor Adwaita; do
        if [ -d "${ROOTFS_DIR}/usr/share/icons/${_theme}" ]; then
            target_run /usr/bin/gtk-update-icon-cache -f -t "/usr/share/icons/${_theme}" 2>/dev/null || true
        fi
    done
else
    echo "[!] No gtk-update-icon-cache in target; libwnck icon lookup may abort" >&2
fi

# XDG compat: Gtk joins $XDG_DATA_DIRS entries with "/mime/..."; with the
# default "/usr/local/share:/usr/share" a missing /usr/local/share/mime
# yields lookups like /usr/local/share/mime//generic-icons (see fault log).
# Keep the dir present so the lookup is empty-but-valid, not ENOENT.
mkdir -p "${ROOTFS_DIR}/usr/local/share/mime/packages" "${ROOTFS_DIR}/usr/local/share/icons" 2>/dev/null || true
if [ -x "${ROOTFS_DIR}/usr/bin/update-mime-database" ]; then
    target_run /usr/bin/update-mime-database /usr/local/share/mime 2>/dev/null || true
fi

# XFCE reverse-DNS rename compat: Arch ships org.xfce.session.png, but
# libwnck/xinitrc-era lookups and weston.ini still request the legacy
# xfce4-session name. A missing name -> NULL base pixbuf -> g_error abort.
# Provide legacy aliases, then rebuild hicolor cache to include them.
if [ -d "${ROOTFS_DIR}/usr/share/icons/hicolor" ]; then
    for _f in "${ROOTFS_DIR}"/usr/share/icons/hicolor/*/apps/org.xfce.session.png; do
        [ -e "${_f}" ] || continue
        _d=$(dirname "${_f}")
        [ -e "${_d}/xfce4-session.png" ] || ln -sf "org.xfce.session.png" "${_d}/xfce4-session.png" 2>/dev/null || true
    done
    mkdir -p "${ROOTFS_DIR}/usr/share/pixmaps" 2>/dev/null || true
    if [ ! -e "${ROOTFS_DIR}/usr/share/pixmaps/xfce4-session.png" ]; then
        _src=$(ls "${ROOTFS_DIR}"/usr/share/icons/hicolor/48x48/apps/org.xfce.session.png \
                 "${ROOTFS_DIR}"/usr/share/icons/hicolor/32x32/apps/org.xfce.session.png 2>/dev/null | head -n 1)
        if [ -n "${_src}" ]; then
            cp -f "${_src}" "${ROOTFS_DIR}/usr/share/pixmaps/xfce4-session.png" 2>/dev/null || true
        fi
    fi
    if [ -x "${ROOTFS_DIR}/usr/bin/gtk-update-icon-cache" ]; then
        target_run /usr/bin/gtk-update-icon-cache -f -t /usr/share/icons/hicolor 2>/dev/null || true
    fi
fi

# Fallback Gtk theme for root (HOME=/) and new users: without this Gtk has
# no icon-theme-name and libwnck gets NULL as its base pixbuf.
mkdir -p "${ROOTFS_DIR}/etc/gtk-3.0" "${ROOTFS_DIR}/root/.config/gtk-3.0" "${ROOTFS_DIR}/etc/skel/.config/gtk-3.0" 2>/dev/null || true
for _gtkini in "${ROOTFS_DIR}/etc/gtk-3.0/settings.ini" "${ROOTFS_DIR}/root/.config/gtk-3.0/settings.ini" "${ROOTFS_DIR}/etc/skel/.config/gtk-3.0/settings.ini"; do
    if [ ! -f "${_gtkini}" ]; then
        cat > "${_gtkini}" <<'EOF'
[Settings]
gtk-icon-theme-name=Adwaita
gtk-fallback-icon-theme=hicolor
gtk-theme-name=Adwaita
EOF
    fi
done

# Weston/XWayland entry point referenced by initrd/weston.ini. Upstream Arch
# ships startxfce4, not this name, so provide a small wrapper with sane
# XDG/GTK defaults for XFCE-on-XWayland.
# Written unconditionally so incremental rootfs trees pick up changes.
echo "[*] Installing start-xfce4-wayland wrapper..."
cat > "${ROOTFS_DIR}/usr/bin/start-xfce4-wayland" <<'EOF'
#!/bin/sh
# AvoryOS: launch XFCE4 inside XWayland (Weston) with icon/mime-safe env.
export XDG_DATA_DIRS="/usr/local/share:/usr/share${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
# Root's home is "/" on AvoryOS. Join the XDG defaults through "${HOME%/}"
# so the session never exports //.config-style paths: WebKitGTK rejects a
# double leading slash as a sandbox root and Epiphany aborts on the
# resulting critical (see docs/epiphany-pulse-launch-abort.md).
_avory_home="${HOME:-/}"
_avory_home="${_avory_home%/}"
export XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-${_avory_home}/.config}"
export XDG_CACHE_HOME="${XDG_CACHE_HOME:-${_avory_home}/.cache}"
export XDG_DATA_HOME="${XDG_DATA_HOME:-${_avory_home}/.local/share}"
export XDG_STATE_HOME="${XDG_STATE_HOME:-${_avory_home}/.local/state}"
unset _avory_home
export NO_AT_BRIDGE=1
export GTK_A11Y=none
if [ -z "$DBUS_SESSION_BUS_ADDRESS" ] && command -v dbus-run-session >/dev/null 2>&1; then
    exec dbus-run-session -- startxfce4 "$@"
else
    exec startxfce4 "$@"
fi
EOF
chmod 0755 "${ROOTFS_DIR}/usr/bin/start-xfce4-wayland"

# Ensure X11/XFCE startup scripts are executable
chmod 0755 "${ROOTFS_DIR}/etc/X11/xinit/xinitrc" 2>/dev/null || true
chmod 0755 "${ROOTFS_DIR}/etc/X11/xinit/xserverrc" 2>/dev/null || true
chmod 0755 "${ROOTFS_DIR}/etc/xdg/xfce4/xinitrc" 2>/dev/null || true

# Ensure dbus helper has user read permission so debugfs can write it into the disk image
chmod u+r "${ROOTFS_DIR}/usr/lib/dbus-daemon-launch-helper" 2>/dev/null || true
chmod 4755 "${ROOTFS_DIR}/usr/lib/dbus-daemon-launch-helper" 2>/dev/null || true

# Wrap xfce4-session in dbus-run-session in xinitrc if present
if [ -f "${ROOTFS_DIR}/etc/xdg/xfce4/xinitrc" ]; then
    sed -i 's/exec \${XFCE4_SESSION_COMPOSITOR:-xfce4-session}/if [ -z "$DBUS_SESSION_BUS_ADDRESS" ] \&\& command -v dbus-run-session >\/dev\/null 2>\&1; then exec dbus-run-session -- \${XFCE4_SESSION_COMPOSITOR:-xfce4-session}; else exec \${XFCE4_SESSION_COMPOSITOR:-xfce4-session}; fi/' "${ROOTFS_DIR}/etc/xdg/xfce4/xinitrc" 2>/dev/null || true
fi

# Canonicalise the XFCE xinitrc XDG fallbacks. Root's home is "/" on
# AvoryOS, so the upstream "$HOME/.config"/"$HOME/.cache" defaults become
# //.config and //.cache. WebKitGTK treats a double leading slash as a
# disallowed sandbox root and Epiphany aborts on the critical that follows
# (docs/epiphany-pulse-launch-abort.md). Join through "${HOME%/}" instead.
if [ -f "${ROOTFS_DIR}/etc/xdg/xfce4/xinitrc" ]; then
    sed -i \
        -e 's#XDG_CONFIG_HOME=\$HOME/\.config#XDG_CONFIG_HOME=${HOME%/}/.config#' \
        -e 's#XDG_CACHE_HOME=\$HOME/\.cache#XDG_CACHE_HOME=${HOME%/}/.cache#' \
        "${ROOTFS_DIR}/etc/xdg/xfce4/xinitrc" 2>/dev/null || true
    if grep -q 'XDG_CONFIG_HOME=\$HOME/\.config\|XDG_CACHE_HOME=\$HOME/\.cache' "${ROOTFS_DIR}/etc/xdg/xfce4/xinitrc" 2>/dev/null; then
        echo "[!] Warning: xfce4 xinitrc still joins XDG defaults with \$HOME; upstream fallback text changed?" >&2
    fi
fi
