#!/usr/bin/env bash
# Fleetwm install script -- Debian 13.6 / Ubuntu & Kubuntu 26.04.
#
# Builds from source and installs to /usr/local. Safe to re-run: ninja
# install is idempotent, and this script doesn't overwrite an existing
# ~/.config/fleetwm/theme.toml.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# scripts/build-pgo-auto.sh (invoked below, see "Building with PGO")
# always builds into build-pgo/, not build/ -- every later reference to
# BUILD_DIR in this script (recording the update path, checking for the
# greeter binary, etc.) has to point at the same directory the actual
# installed binaries were built from.
BUILD_DIR="${SCRIPT_DIR}/build-pgo"

echo "==> Installing build dependencies (requires sudo)"
sudo apt-get update

# NOTE: this is deliberately several separate `apt-get install` calls,
# not one big backslash-continued list -- a `#` comment on its own line
# in the middle of a backslash-continued command silently ends that
# command right there (the comment consumes the rest of its own physical
# line, and since that line has no trailing backslash, nothing continues
# it), so anything listed after such a comment would instead run as its
# own bare, immediate command (e.g. `xwayland`) and fail with "command
# not found" -- fatal here since `set -euo pipefail` is on. Confirmed
# this the hard way: an earlier version of this file had exactly that
# shape and would have failed a truly fresh install.
sudo apt-get install -y \
  build-essential meson ninja-build pkg-config git \
  libwlroots-0.18-dev wayland-protocols libwayland-dev \
  libinput-dev libdrm-dev libxkbcommon-dev libpixman-1-dev \
  libegl1-mesa-dev libgles2-mesa-dev \
  libgtk-4-dev libgtk4-layer-shell-dev \
  libpipewire-0.3-dev \
  libpam0g-dev \
  libsystemd-dev \
  libjemalloc2

# runtime dependency for the bar's power menu (fleetwm-powermenu):
# systemd-logind refuses Sleep/Reboot/Shut down for a non-root caller
# without a running polkit to authorize the request, regardless of
# session state -- fails with "Access denied" even from an active
# session. Log out doesn't need this (ending your own session needs no
# authorization), and sudo bypasses it too (root needs no polkit
# check), which is why those two paths could look like they worked
# while this one silently didn't on a minimal install that never
# pulled polkit in as a transitive dependency of anything else.
# Package name is "polkitd" (Debian 13/trixie) -- the older
# "policykit-1" transitional package no longer exists there; both work
# on Ubuntu 26.04.
sudo apt-get install -y polkitd pkexec

# runtime dependency for the greeter's user-avatar and power-button icons
# (src/greeter-login/login_window.cpp, e.g. avatar-default-symbolic,
# system-reboot-symbolic): those are Adwaita's SVG symbolic icons, and
# GTK4 rasterizes SVG icons through gdk-pixbuf's "svg" loader, which
# ships in librsvg2-common -- not pulled in automatically by
# libgtk-4-dev/adwaita-icon-theme on a minimal install. Without it every
# such icon silently renders as GTK's broken-image/missing-icon glyph
# instead of failing loudly. Confirmed live on real armhf hardware
# (ODROID-XU4): the icon *names* were always correct, installing this
# package alone fixed every broken icon on the login screen.
sudo apt-get install -y librsvg2-common

sudo apt-get install -y xwayland foot

# end-user runtime: Alt+Shift+S's screenshot keybind
# (compositor/input.cpp's kScreenshotCommand) -- grim captures, slurp
# picks the region, wl-copy puts it on the clipboard, notify-send
# confirms it
sudo apt-get install -y grim slurp wl-clipboard libnotify-bin

# dev-box testing: pixel inspection (imagemagick's `convert ...
# txt:-`) + synthetic pointer/keyboard input (wlrctl/wtype) over SSH,
# since fleetwm advertises wlr-screencopy-v1, wlr-virtual-pointer-v1,
# and wlr-virtual-keyboard-v1 for exactly this; gdb for live-attaching
# to the compositor to catch crashes
sudo apt-get install -y wlrctl wtype gdb imagemagick

# build-time only: scripts/build-pgo-auto.sh's synthetic PGO training
# pass (now run unconditionally below, see "Building with PGO") needs
# `dbus-run-session` (an isolated session bus, so the training run's
# GTK4 clients don't silently hand off to a real desktop session's bus
# instead of doing any work) and python3 (already present on every
# supported distro here, listed for completeness).
sudo apt-get install -y dbus-daemon python3

echo "==> Setting system default locale to C.UTF-8"
# fleetwm-greet's session env (src/greeter/session.cpp) also hardcodes
# this as a floor for every fleetwm session regardless of the system
# default, but setting it here too keeps outside-of-fleetwm logins (a
# plain TTY, SSH) consistent instead of inheriting whatever partial/
# unavailable locale (e.g. a language locale that was never actually
# generated on this machine) came from the base install. C.UTF-8 rather
# than a real language locale since it's guaranteed present on every
# glibc system with no locale-gen step required.
sudo update-locale LANG=C.UTF-8 LC_ALL=C.UTF-8 LANGUAGE=

echo "==> Building with PGO (profile-guided optimization)"
# Every install now goes through the full instrumented-build ->
# synthetic-training -> profile-optimized-rebuild pipeline
# (scripts/build-pgo-auto.sh), not just a single release build --
# mandatory, not opt-in, per explicit user request. This takes longer
# than a plain build (compiles twice, plus a training pass -- budget an
# extra minute or two on top of a normal build), but every installed
# binary ends up profile-guided rather than only the ones someone
# happened to PGO-train by hand. See build-pgo-auto.sh/build-pgo.sh/
# pgo-train-session.sh for the full flag rationale (LTO, -march=native,
# -DG_DISABLE_ASSERT, full RELRO, etc. -- all still applied, PGO is
# layered on top of the same release-build flags this script used
# before) and exactly what the synthetic training pass exercises.
#
# Both the instrumented and final builds run the unit test suite
# themselves (build-pgo.sh's own `meson test` call, `set -euo pipefail`
# propagating failure) -- a failing test aborts here, before any
# installed binary is touched, same "tests gate the install" contract
# as before.
bash "${SCRIPT_DIR}/scripts/build-pgo-auto.sh"

echo "==> Installing (requires sudo)"
sudo ninja -C "${BUILD_DIR}" install

echo "==> Recording source checkout path for 'fleetwm update'"
echo "${SCRIPT_DIR}" | sudo tee /etc/fleetwm-source-path >/dev/null

echo "==> Adding $(whoami) to device-access groups (input/video/render/audio)"
# The compositor needs libinput device access (input), DRM/GPU access
# (video, and render on distros that split it out), and PipeWire/audio
# device access (audio) to function -- without these, keyboard/mouse
# events or rendering can silently fail with no visible error, since
# open() on a permission-denied /dev node just makes libinput/wlroots see
# no device rather than raising an obvious error. plugdev covers some
# USB peripherals (e.g. certain webcams/removable media) on Debian-family
# systems that gate them separately from video. Only add groups that
# actually exist on this system -- not all of these exist on every distro
# or hardware configuration.
for group in input video render audio plugdev; do
  if getent group "${group}" >/dev/null 2>&1; then
    sudo usermod -aG "${group}" "$(whoami)"
  fi
done
echo "(If any of these groups were newly added, log out and back in --"
echo "or reboot -- for group membership to take effect.)"

if [[ -x "${BUILD_DIR}/src/greeter/fleetwm-greet" ]]; then
  # fleetwm-greet now runs its own wlroots compositor for the login UI
  # (src/greeter/compositor.{hpp,cpp}), which needs a seat backend to get
  # DRM access before anyone's logged in -- seatd, not logind (see
  # packaging/fleetwm-greeter@.service's own comment for why logind
  # specifically doesn't work here: it segfaults on a real login).
  echo "==> Installing seatd (required by the greeter's login-screen compositor)"
  sudo apt-get install -y seatd
  sudo systemctl enable --now seatd.service

  echo "==> Installing greeter PAM config and systemd unit"
  sudo install -m 644 "${SCRIPT_DIR}/packaging/fleetwm-greeter-pam.conf" /etc/pam.d/fleetwm-greeter
  sudo install -m 644 "${SCRIPT_DIR}/packaging/fleetwm-greeter@.service" /usr/lib/systemd/system/fleetwm-greeter@.service
  sudo systemctl daemon-reload

  # fleetwm-locker (the Lock power-menu action) re-verifies the running
  # user's password via its own PAM service -- separate from
  # fleetwm-greeter's above since it never opens a session (pam_unix +
  # pam_systemd's session lines make no sense for re-auth of an
  # already-running session).
  echo "==> Installing locker PAM config"
  sudo install -m 644 "${SCRIPT_DIR}/packaging/fleetwm-locker-pam.conf" /etc/pam.d/fleetwm-locker
fi

echo
echo "Fleetwm installed. Log out and select 'Fleetwm' from your display"
echo "manager's session list to start using it."
echo "Run 'fleetwm update' at any time to pull and rebuild the latest version."

if [[ -x "${BUILD_DIR}/src/greeter/fleetwm-greet" ]]; then
  echo
  echo "Fleetwm greeter (fleetwm-greet) installed but NOT enabled. To use it"
  echo "instead of a display manager on your main console (tty1):"
  echo "  sudo systemctl disable --now getty@tty1.service"
  echo "  sudo systemctl enable --now fleetwm-greeter@tty1.service"
  echo "Then switch to that VT (Ctrl+Alt+F1) to see the login prompt."
  echo "(Use a different ttyN above if you'd rather leave tty1's normal"
  echo "login console alone.)"
fi
