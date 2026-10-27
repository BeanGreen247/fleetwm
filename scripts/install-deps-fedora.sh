#!/usr/bin/env bash
# Build and test dependencies for Fedora and Nobara (dnf). install.sh is Debian-only; this covers compiling the tree and running the
# unit tests (scripts/run-tests.sh), it does not install the greeter or change the display manager.
# Verified on Nobara 44 with wlroots 0.20.2 (the code builds against wlroots 0.18 to 0.20).
set -euo pipefail
sudo dnf install -y --setopt=install_weak_deps=False \
  gcc-c++ meson ninja-build pkgconf git wlroots-devel wayland-devel wayland-protocols-devel \
  libinput-devel libxkbcommon-devel libdrm-devel pixman-devel "pkgconfig(egl)" "pkgconfig(glesv2)" \
  pipewire-devel pam-devel systemd-devel openssl-devel jemalloc gtest-devel cairo-devel libpng-devel \
  libjpeg-turbo-devel libwebp-devel fontconfig-devel fuse3-devel libcurl-devel libssh2-devel \
  libsmbclient-devel zlib-ng-compat-devel libxcb-devel xcb-util-wm-devel
