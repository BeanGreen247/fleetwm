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
  libwlroots-dev wayland-protocols libwayland-dev \
  libinput-dev libdrm-dev libxkbcommon-dev libpixman-1-dev \
  libegl1-mesa-dev libgles2-mesa-dev \
  libgtk-4-dev libgtk4-layer-shell-dev \
  libpipewire-0.3-dev \
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

echo
echo "Fleetwm installed. Log out and select 'Fleetwm' from your display"
echo "manager's session list to start using it."
echo "Run 'fleetwm update' at any time to pull and rebuild the latest version."
