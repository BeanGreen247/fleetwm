#!/bin/sh
# Fleetwm install script for Alpine Linux (3.21 or newer, musl, OpenRC, elogind, seatd).
# Builds from source and installs to /usr/local, like install.sh does on Debian. Safe to re-run.
#   FLEETWM_NO_GREETER=1   do not take over the first console with the Fleetwm login screen
#   FLEETWM_GREETER_TTY=tty2  console for the login screen (default tty1)
#   FLEETWM_PIXMAN_ONLY=1  low-memory build: wlroots with the software renderer only, no Mesa/LLVM (no GPU drawing, no XWayland)
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-pgo"
TTY="${FLEETWM_GREETER_TTY:-tty1}"

[ -r /etc/alpine-release ] || { echo "error: this script is for Alpine Linux; use install.sh on Debian and Ubuntu" >&2; exit 1; }
if [ "$(id -u)" -eq 0 ]; then SUDO=""; elif command -v doas >/dev/null 2>&1; then SUDO="doas"; else SUDO="sudo"; fi

echo "==> Installing the build tools and libraries (apk)"
PIXMAN_ONLY="${FLEETWM_PIXMAN_ONLY:-0}"
if [ "$PIXMAN_ONLY" = "1" ]; then
  GFX="libseat-dev eudev-dev hwdata-dev libdisplay-info-dev libliftoff-dev curl bzip2"  # wlroots is built from source below, without Mesa
else
  GFX="wlroots-dev mesa-dev mesa-dri-gallium"
fi
$SUDO apk add --no-cache \
  build-base meson samurai pkgconf git python3 bash tar $GFX \
  wayland-dev wayland-protocols libinput-dev libxkbcommon-dev libdrm-dev pixman-dev \
  cairo-dev pipewire-dev wireplumber elogind-dev linux-pam-dev openssl-dev \
  fontconfig-dev libpng-dev libjpeg-turbo-dev libwebp-dev zlib-dev xcb-util-wm-dev libxcb-dev \
  fuse3-dev curl-dev libssh2-dev \
  font-inter font-dejavu xkeyboard-config openrc seatd seatd-openrc elogind elogind-openrc polkit-elogind polkit-openrc \
  udisks2 eudev eudev-openrc dbus dbus-openrc
# bash and GNU tar: Fleetwm's own scripts and the settings backup use them (busybox tar has no --sort).

MESON_EXTRA=""
if [ "$PIXMAN_ONLY" = "1" ]; then
  echo "==> Building wlroots 0.18 with the software renderer only"
  FLEETWM_PIXMAN_ONLY=1 $SUDO bash "${SCRIPT_DIR}/scripts/build-wlroots-from-source.sh" /usr/local
  MESON_EXTRA="-Dxwayland=false"
fi

echo "==> Profile-guided build (instrumented build, a training run on a virtual screen, optimized build)"
for tool in wtype wlrctl foot grim; do command -v "$tool" >/dev/null 2>&1 || $SUDO apk add "$tool" >/dev/null 2>&1 || true; done  # the training run uses them; without one that part of the training is skipped
BUILD_DIR="${SCRIPT_DIR}/build-pgo"
FLEETWM_PGO_DIR="${BUILD_DIR}" FLEETWM_PREFIX=/usr/local FLEETWM_MESON_EXTRA="$MESON_EXTRA" bash "${SCRIPT_DIR}/scripts/build-pgo-auto.sh"

echo "==> Installing to /usr/local"
# meson install --no-rebuild: root must not run ninja in the build folder (it would leave root-owned files there)
$SUDO meson install -C "${BUILD_DIR}" --no-rebuild >/dev/null

echo "==> Pointer: GTK programs use Fleetwm's own cursor theme"
if [ "$(id -u)" -eq 0 ] && [ -n "${SUDO_USER:-}" ]; then sudo -u "${SUDO_USER}" -H bash "${SCRIPT_DIR}/scripts/set-cursor-theme.sh" || true
else bash "${SCRIPT_DIR}/scripts/set-cursor-theme.sh" || true; fi
if [ "$(id -u)" -eq 0 ] && [ -n "${SUDO_USER:-}" ]; then sudo -u "${SUDO_USER}" -H bash "${SCRIPT_DIR}/scripts/electron-wayland.sh" || true
else bash "${SCRIPT_DIR}/scripts/electron-wayland.sh" || true; fi

echo "==> Services: dbus, elogind and seatd at boot"
for svc in dbus elogind seatd udev polkit; do
  $SUDO rc-update add "$svc" default >/dev/null 2>&1 || true
  $SUDO rc-service "$svc" start >/dev/null 2>&1 || true
done

echo "==> Groups for input, GPU and sound"
for group in input video render audio seat; do
  if getent group "$group" >/dev/null 2>&1; then $SUDO addgroup "$(id -un)" "$group" >/dev/null 2>&1 || true; fi
done

echo "==> Login screen"
$SUDO install -m 644 "${SCRIPT_DIR}/packaging/fleetwm-locker-pam.conf" /etc/pam.d/fleetwm-locker
if [ -x "${BUILD_DIR}/src/greeter/fleetwm-greet" ] && [ "${FLEETWM_NO_GREETER:-0}" != "1" ]; then
  $SUDO install -m 644 "${SCRIPT_DIR}/packaging/fleetwm-greeter-pam-alpine.conf" /etc/pam.d/fleetwm-greeter
  $SUDO install -m 755 "${SCRIPT_DIR}/packaging/fleetwm-greeter-run" /usr/local/bin/fleetwm-greeter-run
  entry="${TTY}::respawn:/usr/local/bin/fleetwm-greeter-run ${TTY}"
  if ! grep -qF "fleetwm-greeter-run ${TTY}" /etc/inittab; then
    # replace the text login on that console; the other consoles keep theirs
    $SUDO sed -i "s|^${TTY}::.*getty.*|${entry}|" /etc/inittab
    grep -qF "fleetwm-greeter-run ${TTY}" /etc/inittab || echo "${entry}" | $SUDO tee -a /etc/inittab >/dev/null
  fi
  echo "The Fleetwm login screen replaces the text login on ${TTY} after the next boot (or: $SUDO kill -HUP 1)."
  echo "To go back, restore the 'getty' line for ${TTY} in /etc/inittab."
else
  echo "Login screen left alone. Start a session from a console with: dbus-run-session fleetwm"
fi
echo "Fleetwm installed. Log out and back in once so the new group memberships apply."
