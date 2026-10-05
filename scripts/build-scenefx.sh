#!/bin/sh
# Builds and installs SceneFX 0.2.1 (wlroots scene API + effects renderer) so
# fleetwm can draw rounded window corners and shadows. Needs wlroots 0.18,
# meson, ninja, libegl/libgles2 dev packages. Installs to /usr/local.
set -e
tmp=$(mktemp -d)
cd "$tmp"
curl -fsSL https://github.com/wlrfx/scenefx/archive/refs/tags/0.2.1.tar.gz | tar xz
cd scenefx-0.2.1
meson setup build -Dprefix=/usr/local
ninja -C build
sudo ninja -C build install
sudo ldconfig
