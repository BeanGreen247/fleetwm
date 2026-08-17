#!/usr/bin/env bash
# Fleetwm install script -- Debian 13.6 / Ubuntu & Kubuntu 26.04.
#
# Builds from source and installs to /usr/local. Safe to re-run: ninja
# install is idempotent, and this script doesn't overwrite an existing
# ~/.config/fleetwm/theme.toml.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

echo "==> Installing build dependencies (requires sudo)"
sudo apt-get update
sudo apt-get install -y \
  build-essential meson ninja-build pkg-config git \
  libwlroots-0.18-dev wayland-protocols libwayland-dev \
  libinput-dev libdrm-dev libxkbcommon-dev libpixman-1-dev \
  libegl1-mesa-dev libgles2-mesa-dev \
  libgtk-4-dev libgtk4-layer-shell-dev \
  libpipewire-0.3-dev \
  libpam0g-dev \
  xwayland \
  foot \
  grim slurp wl-clipboard libnotify-bin \
                      # end-user runtime: Alt+Shift+S's screenshot keybind
                      # (compositor/input.cpp's kScreenshotCommand) --
                      # grim captures, slurp picks the region, wl-copy
                      # puts it on the clipboard, notify-send confirms it
  wlrctl wtype gdb imagemagick  # dev-box testing: pixel inspection
                      # (imagemagick's `convert ... txt:-`) + synthetic
                      # pointer/keyboard input (wlrctl/wtype) over SSH,
                      # since fleetwm advertises wlr-screencopy-v1,
                      # wlr-virtual-pointer-v1, and wlr-virtual-keyboard-v1
                      # for exactly this; gdb for live-attaching to the
                      # compositor to catch crashes

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

echo "==> Configuring build"
# -Db_ndebug=true: meson's "release" buildtype alone does NOT disable
# assert() (that's a separate option) -- every assert-guarded check in
# the compositor's hot paths (input, render, layout) would otherwise
# still pay its runtime cost in a "release" build. -Dstrip=true: strips
# debug symbols from the installed binaries (meson's buildtype also
# doesn't imply this). Neither changes behavior, both are standard
# release-build hygiene -- part of the standing "run as efficiently as
# possible" goal, not a one-off tweak.
# -Db_lto=true: whole-program link-time optimization across every
# translation unit in each binary (cross-TU inlining, better dead-code
# elimination) -- meson/ninja handle this natively, no manual flag
# wrangling needed. -march=native: codegen tuned for the exact CPU this
# is building on, safe ONLY because install.sh always builds fresh on
# the machine it installs to (never cross-compiled or redistributed as a
# prebuilt binary) -- a binary built this way will refuse to run
# correctly on a different CPU model, which is fine here but would NOT
# be fine for e.g. a .deb built once and shipped to arbitrary machines.
# -ffunction-sections/-fdata-sections + -Wl,--gc-sections: puts each
# function/global in its own linker section so the linker can drop ones
# nothing calls -- LTO already does cross-TU dead-code elimination, but
# this catches unused code inside libraries/headers LTO doesn't see
# through (e.g. template instantiations, inline helpers pulled in by a
# system header) and is essentially free to add alongside it.
# -fno-semantic-interposition: tells GCC this is a plain executable, not
# something another library might override symbols in at load time, so
# it can inline/optimize across function-call boundaries as aggressively
# as LTO's own analysis allows instead of conservatively assuming any
# call could be interposed -- meaningful free win specifically because
# LTO is already on (this flag's benefit is largest exactly when
# whole-program analysis is already happening).
meson setup "${BUILD_DIR}" "${SCRIPT_DIR}" --prefix=/usr/local --buildtype=release \
  -Db_ndebug=true -Dstrip=true -Db_lto=true \
  -Dc_args='-march=native -ffunction-sections -fdata-sections -fno-semantic-interposition' \
  -Dcpp_args='-march=native -ffunction-sections -fdata-sections -fno-semantic-interposition' \
  -Dc_link_args=-Wl,--gc-sections -Dcpp_link_args=-Wl,--gc-sections \
  --reconfigure

echo "==> Building"
ninja -C "${BUILD_DIR}"

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
