#!/usr/bin/env bash
# Installs perf and KDAB Hotspot on a workstation or VM and makes `perf record` usable by a normal user, then prints what to do next (docs/HOTSPOT_PROFILING.md).
# Debian/Ubuntu: linux-perf plus the Hotspot AppImage in ~/.local/opt/hotspot (Debian does not package Hotspot). Fedora/Nobara: dnf install hotspot perf.
#   scripts/setup-hotspot.sh [--no-sysctl]       --no-sysctl leaves perf_event_paranoid alone
set -euo pipefail
HOTSPOT_VERSION="${HOTSPOT_VERSION:-v1.6.0}"
. /etc/os-release
case "${ID:-} ${ID_LIKE:-}" in
  *fedora*|*rhel*|*nobara*)
    sudo dnf install -y --setopt=install_weak_deps=False perf hotspot gdb ;;
  *debian*|*ubuntu*)
    sudo apt-get install -y linux-perf gdb curl 2>/dev/null || sudo apt-get install -y linux-tools-common linux-tools-generic gdb curl
    if ! command -v hotspot >/dev/null 2>&1; then
      dir="${HOME}/.local/opt/hotspot"; mkdir -p "${dir}" "${HOME}/.local/bin"
      curl -fL -o "${dir}/hotspot-${HOTSPOT_VERSION}-x86_64.AppImage" "https://github.com/KDAB/hotspot/releases/download/${HOTSPOT_VERSION}/hotspot-${HOTSPOT_VERSION}-x86_64.AppImage"
      chmod +x "${dir}/hotspot-${HOTSPOT_VERSION}-x86_64.AppImage"
      ln -sf "${dir}/hotspot-${HOTSPOT_VERSION}-x86_64.AppImage" "${HOME}/.local/bin/hotspot"
      [[ -e /dev/fuse ]] || echo "note: no /dev/fuse here; start Hotspot with: hotspot --appimage-extract-and-run"
    fi ;;
  *) echo "unsupported distribution; install perf and Hotspot (github.com/KDAB/hotspot) by hand" >&2; exit 1 ;;
esac
if [[ ${1:-} != --no-sysctl ]]; then
  echo "kernel.perf_event_paranoid=1 / kptr_restrict=0 for this boot (add a file in /etc/sysctl.d/ to keep it)"
  sudo sysctl -w kernel.perf_event_paranoid=1 kernel.kptr_restrict=0
fi
echo "perf: $(perf --version 2>&1 | head -1)   hotspot: $(command -v hotspot || echo 'not on PATH (~/.local/bin?)')"
echo "Try:  perf record -F 2000 --call-graph dwarf -o /tmp/t.data -- ls -R /usr >/dev/null && hotspot /tmp/t.data"
