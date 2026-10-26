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
# FLEETWM_PIXMAN_ONLY=1: the low-memory build (docs/ALPINE.md): software renderer only, no EGL, GBM or Mesa, no XWayland. On Alpine this keeps
# libLLVM out of the compositor (53.8 MB -> 12.3 MB), and it is faster than GLES2 on weak integrated GPUs; there is no GPU drawing.
if [ "${FLEETWM_PIXMAN_ONLY:-0}" = "1" ]; then
  meson setup build --prefix="$prefix" --buildtype=release \
    -Dexamples=false -Dxwayland=disabled -Drenderers= -Dbackends=drm,libinput -Dallocators=
else
  meson setup build --prefix="$prefix" --buildtype=release \
    -Dexamples=false -Dxwayland=enabled -Drenderers=gles2 -Dbackends=drm,libinput,x11 -Dallocators=gbm
fi
ninja -C build
ninja -C build install
ldconfig 2>/dev/null || true  # no ldconfig on musl
echo "wlroots ${version} installed in ${prefix}"
