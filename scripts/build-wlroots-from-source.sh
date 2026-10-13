#!/bin/bash
# Build and install wlroots 0.18 into a private prefix, for Debian 12 (bookworm), which ships
# wlroots 0.15. wlroots 0.18 needs wayland >= 1.23, libdrm >= 2.4.122 and wayland-protocols; on
# bookworm take those from bookworm-backports first (see docs/DEBIAN_BACKPORT.md).
#
#   scripts/build-wlroots-from-source.sh [PREFIX]      (default /usr/local; run as root)
#   /usr/local is on Debian's default pkg-config and ld.so search paths, so nothing else needs setting.
set -euo pipefail
version=0.18.2
prefix=${1:-/usr/local}
work=$(mktemp -d)
trap 'cd /; rm -rf -- "$work"' EXIT
cd "$work"
curl -fsSL -o wlroots.tar.bz2 "https://deb.debian.org/debian/pool/main/w/wlroots/wlroots_${version}.orig.tar.bz2"
tar xf wlroots.tar.bz2
cd "wlroots-${version}"
meson setup build --prefix="$prefix" --buildtype=release \
  -Dexamples=false -Dxwayland=enabled -Drenderers=gles2 -Dbackends=drm,libinput,x11 -Dallocators=gbm
ninja -C build
ninja -C build install
ldconfig
echo "wlroots ${version} installed in ${prefix}"
