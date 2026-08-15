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
  foot

echo "==> Configuring build"
meson setup "${BUILD_DIR}" "${SCRIPT_DIR}" --prefix=/usr/local --buildtype=release --reconfigure

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
  echo "==> Installing greeter PAM config and systemd unit"
  sudo install -m 644 "${SCRIPT_DIR}/packaging/fleetwm-greeter-pam.conf" /etc/pam.d/fleetwm-greeter
  sudo install -m 644 "${SCRIPT_DIR}/packaging/fleetwm-greeter@.service" /usr/lib/systemd/system/fleetwm-greeter@.service
  sudo systemctl daemon-reload
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
