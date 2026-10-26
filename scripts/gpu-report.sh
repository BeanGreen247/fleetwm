#!/usr/bin/env bash
# gpu-report.sh: what graphics hardware (real or virtual) this machine has and which renderer Fleetwm will use on it.
# Read-only, needs no root. Run it on a new laptop, in a VM (Proxmox: Display = VirtIO-GPU or VirGL GPU, or Standard VGA), or
# when "the screen is slow / black / has no cursor", and send the output along.
#
#   scripts/gpu-report.sh
set -uo pipefail

section() { printf '\n== %s ==\n' "$1"; }

section "Machine"
printf 'virtualisation : %s\n' "$(systemd-detect-virt 2>/dev/null || echo unknown)"
printf 'product        : %s %s\n' "$(cat /sys/class/dmi/id/sys_vendor 2>/dev/null)" "$(cat /sys/class/dmi/id/product_name 2>/dev/null)"
printf 'kernel         : %s\n' "$(uname -r)"
printf 'cpu            : %s (%s threads)\n' "$(awk -F: '/model name/{print $2; exit}' /proc/cpuinfo | sed 's/^ //')" "$(nproc)"
awk '/^MemTotal/{printf "memory         : %.0f MB\n", $2/1024}' /proc/meminfo

section "Display devices (DRM)"
virtual_gpu=0
virgl=unknown
for card in /sys/class/drm/card[0-9]*; do
  [ -d "$card/device" ] || continue
  name=$(basename "$card")
  case "$name" in *-*) continue ;; esac  # card0-Virtual-1 is a connector, not a card
  drv=$(basename "$(readlink -f "$card/device/driver" 2>/dev/null)" 2>/dev/null)
  ven=$(cat "$card/device/vendor" 2>/dev/null); dev=$(cat "$card/device/device" 2>/dev/null)
  kind=real
  case "$drv" in
    virtio_gpu|virtio-pci|vmwgfx|qxl|bochs|bochs-drm|cirrus|cirrus-qemu|vboxvideo|hyperv_drm|simpledrm|vkms|ast|mgag200) kind=virtual-or-basic ;;
  esac
  [ "$kind" != real ] && virtual_gpu=1
  printf '%-7s driver %-12s pci %s:%s  (%s)\n' "$name" "${drv:-?}" "${ven:-?}" "${dev:-?}" "$kind"
  for c in "$card"-*; do [ -e "$c/status" ] && printf '          %s: %s\n' "$(basename "$c")" "$(cat "$c/status")"; done
  if [ "$drv" = virtio_gpu ] || [ "$drv" = virtio-pci ]; then
    # the virtio-gpu device's feature bits: bit 0 = virgl (3D through the host's GPU), bit 1 = EDID
    for v in /sys/bus/virtio/devices/virtio*; do
      [ "$(cat "$v/device" 2>/dev/null)" = "0x0010" ] || continue
      f=$(cat "$v/features" 2>/dev/null)
      if [ "${f:0:1}" = 1 ]; then virgl=yes; else virgl=no; fi
      printf '          virtio-gpu features: %s  -> 3D (virgl): %s\n' "$f" "$virgl"
    done
  fi
done
printf '\n/dev/dri : %s\n' "$(ls /dev/dri 2>/dev/null | tr '\n' ' ')"
render_node=no; ls /dev/dri/renderD* >/dev/null 2>&1 && render_node=yes
printf 'render node present: %s\n' "$render_node"
printf 'you are in groups  : %s\n' "$(id -nG)"

section "Mesa / OpenGL ES"
if command -v eglinfo >/dev/null 2>&1; then
  eglinfo -B 2>/dev/null | grep -E "^(GBM|Surfaceless|Wayland|Device).*platform|OpenGL ES profile (version|renderer)|OpenGL renderer|EGL driver name" | head -12
elif command -v glxinfo >/dev/null 2>&1; then
  glxinfo -B 2>/dev/null | grep -E "renderer string|OpenGL ES profile version" | head
else
  echo "(no eglinfo or glxinfo installed: apt install mesa-utils-bin shows which GL driver is in use)"
fi
ls /usr/lib/*/dri/virtio_gpu_dri.so >/dev/null 2>&1 && echo "virgl driver file: present (virtio_gpu_dri.so)" || echo "virgl driver file: not found (Mesa has no virtio-gpu 3D driver installed)"
dpkg -l 2>/dev/null | awk '/^ii +(libgl1-mesa-dri|mesa-libgallium|libegl-mesa0|libgbm1) /{printf "%s %s\n", $2, $3}'

section "What Fleetwm will do"
if [ -n "${WLR_RENDERER:-}" ]; then
  echo "WLR_RENDERER=$WLR_RENDERER is set in this shell and decides (Fleetwm honours it)."
elif [ "$render_node" = no ]; then
  echo "No render node: the pixman (CPU) renderer. Fine for a desktop; no GPU is used."
elif [ "$virtual_gpu" = 1 ] && [ "$virgl" != yes ]; then
  echo "Virtual GPU without 3D: wlroots refuses Mesa's software GL (llvmpipe), so Fleetwm ends on the pixman (CPU) renderer."
  echo "  Proxmox: set the VM's Display to 'VirGL GPU' for GPU rendering (the host needs libvirglrenderer and a GPU/EGL)."
elif [ "$virgl" = yes ]; then
  echo "virtio-gpu with 3D (virgl): the GLES2 renderer through Mesa's virgl driver (GPU rendering on the host)."
else
  echo "A GPU with a render node: the GLES2 renderer (hardware), pixman only if EGL fails."
fi
echo "Force one for a test:  WLR_RENDERER=pixman fleetwm   or   WLR_RENDERER=gles2 fleetwm"
echo "The compositor writes its choice to the session log as 'fleetwm: renderer ...'."
