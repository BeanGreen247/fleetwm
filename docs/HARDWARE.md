# Supported hardware

Architecture support is meant to be general -- nothing in fleetwm is
written against a specific CPU or GPU vendor -- but the table below is
what has actually been run and verified, not just assumed to work.

All single-board-computer testing is done and provided by this repo's
owner, using [eqvaldi/releases V4-LTS-3](https://github.com/eqvaldi/releases/releases/tag/V4-LTS-3)
as the SBC OS base.

| Platform | Arch | GPU rendering | Status |
| --- | --- | --- | --- |
| Generic x86_64 (QEMU/KVM VM, `fleetwm-dev`) | x86\_64 | llvmpipe (software, no virtio-GPU 3D) | Primary development target; verified continuously |
| Laptop, Intel Celeron N4020 (2 cores), UHD Graphics 600, 4 GB RAM, Debian 13 | x86\_64 | Hardware GLES2 through Mesa 25.0.7 on the `i915` kernel driver (session log: "GL renderer: Mesa Intel(R) UHD Graphics 600 (GLK 2)", OpenGL ES 3.2, eDP-1) | **Real-hardware testing bed** (since 2026-10). Checked on 2026-10-05 with the build from commit `1776b66`: the install script (PGO build plus all unit tests on the machine, about 25 minutes), login screen, desktop layout at 1920x1080, taskbar, dynamic workspace buttons, the Power page with the real battery, the `~>` terminal prompt, the battery icon opening the Power page, and the power actions (`reboot`, `poweroff`). The compositor ran at nice -10 with 96 MB resident and about 0.4% of one core when idle. It is also the slow-machine reference for performance and memory work |
| Laptop, ThinkPad E490 (20N9S19A00), Intel Core i5-8265U (4 cores, 8 threads), UHD Graphics 620, 39 GB RAM, Debian 13 | x86\_64 | Hardware GLES2 through Mesa 25.0.7 on the `i915` kernel driver | Real-hardware install and daily use (2026-10-20); install timings in `docs/PERFORMANCE_FINDINGS.md` |
| Exynos5422 (Mali-T628 MP6) | armhf | pixman (software) -- kernel has no Panfrost driver built in ([`CONFIG_DRM_PANFROST`](https://docs.mesa3d.org/drivers/panfrost.html) unset), so GLES2/EGL init fails and the compositor falls back automatically | Verified end-to-end on real hardware (2026-08-18): greeter, login, bar, wallpaper all confirmed working over the software renderer |
| Asus Tinker Board (Rockchip RK3288) | armv7l | untested -- build never completed | **Blocked, hardware issue, not a fleetwm bug** (2026-08-19): a full-parallelism `ninja` build (4 jobs) on this board's 2GB RAM caused it to drop off the network entirely mid-compile -- reproduced, not a one-off. This board's Micro-USB power input is a [documented Armbian undervoltage/brownout issue](https://www.hometutoring.co.nz/electronics/rpi_alt.php#:~:text=Armbian%20notes%3A%20%22Severe%20powering%20troubles%20due%20to%20Micro%20USB%20power%20connector.%20It%27s%20recommended%20to%20power%20through%20GPIO%20pins%20to%20prevent%20under%2Dvoltage%20issues%20(instabilities%2C%20boot/crash%20cycles).%20Powering%20situation%20is%20a%20little%20improved/masked%20on%20model%20S.%22), not something the compositor code can fix. If retrying: power via the GPIO pins instead of Micro-USB, and cap build parallelism (`ninja -j1`/`-j2`) to reduce peak draw. |
| Amlogic S922X (ODROID-N2, Mali-G52 MP4) | aarch64 | not yet confirmed which renderer is in use (GLES2/Panfrost vs. pixman fallback) | Verified end-to-end on real hardware (2026-08-19): full mandatory PGO install pipeline, 243/243 unit tests passing directly on-device, real login/desktop use with an external monitor attached |

Any GPU whose driver fails hardware EGL initialization (not just one
missing an extension) gets the same automatic pixman software-rendering
fallback the Exynos5422 board uses above -- see
`wlr_pixman_renderer_create()` in `src/compositor/server.cpp`. That
keeps the desktop usable, just without GPU acceleration, on hardware
like older/unsupported Mali Midgard GPUs.


## Virtual GPUs and Proxmox (analysis 2026-10-26)

What is verified: the dev VM is a QEMU/KVM guest (Proxmox) with **Standard VGA** (`bochs-drm`, PCI 1234:1111, no render node). Fleetwm
starts on the pixman (CPU) renderer there and has been run on it continuously. **VirtIO-GPU and VirGL have not been run**: nothing in
this repository was ever started on a `virtio_gpu` device, so everything below is reading the code, not a test.

How the code chooses (`src/compositor/server.cpp`, "renderer"): an explicit `WLR_RENDERER` always wins; with no `/dev/dri/renderD*`
(Standard VGA, headless) it uses pixman; otherwise wlroots tries GLES2 and, if that fails or only Mesa's software rasteriser
(llvmpipe) is available, Fleetwm falls back to pixman. The session log now says which: `fleetwm: renderer GLES2 (GPU); display
devices: card0=virtio_gpu`.

| Proxmox VM display | Guest sees | Expected renderer | Notes |
|---|---|---|---|
| Standard VGA (default) | `bochs-drm`, no render node | pixman | verified |
| VirtIO-GPU (no 3D) | `virtio_gpu`, a render node, feature bit 0 (virgl) off | pixman, chosen by Fleetwm (`only_software_gpus()`) | **verified 2026-10-26** on the dev VM: compositor 21 MB and 0.4% CPU; before the change it ran Mesa's llvmpipe because the greeter sets `WLR_RENDERER_ALLOW_SOFTWARE=1`: 146 MB and 8.4% CPU |
| VirGL GPU | `virtio_gpu` with virgl 3D | GLES2 through Mesa's virgl driver (host GPU) | needs `libvirglrenderer` and an EGL-capable GPU on the Proxmox host; the guest needs Mesa >= 21 (Debian 13 has 25.0, the `virtio_gpu_dri.so` driver is present) |
| VMware compatible | `vmwgfx` | pixman or GLES2 (svga3d) | not looked at |
| Intel/AMD/NVIDIA passthrough (vfio) | the real driver | GLES2 | same as the laptops below |

Update 2026-10-26: the dev VM was switched to VirtIO-GPU (PCI 1af4:1050, render node `renderD128`, virgl off). The first run showed the
trap this analysis suspected, in a worse form: `fleetwm-greet` starts the session with `WLR_RENDERER_ALLOW_SOFTWARE=1`, so wlroots accepted
llvmpipe and the compositor ran software GL (146 MB PSS, 8.4% of a core with one terminal scrolling 10 lines a second). Fleetwm now picks pixman
when every display device is basic or virtual without 3D (`only_software_gpus()` in `src/compositor/server.cpp`: Standard VGA, Cirrus, QXL,
VirtualBox, Hyper-V, firmware framebuffers, virtio-gpu without the virgl feature): compositor 21.4 MB and 0.4% CPU on the same scene (compositor
-125 MB, CPU /21). VirGL (3D) was not available on this VM, so GLES2 on virgl is still untested.

Risks found by reading the code, to be measured before anything is changed:
1. **Glass read-back.** `glass_scene.cpp` reads window pixels with `wlr_texture_read_pixels` (a `glReadPixels` on GLES2) every 45-120 ms
   while glass windows overlap repainting windows. On virgl every read is a round trip to the host GPU process, so glass could cost far more
   than on a real GPU (the notes already record GLES2 at 9x the compositor CPU of pixman on the Celeron, mostly a Mesa copy). If it does, the
   fix is a policy (no window read-back on virtual GPUs, or pixman by default there), not a rewrite.
2. **Frame pacing and damage** rely on `wlr_output` presentation feedback; virtio-gpu's page flips complete on the host's schedule, which
   `FLEETWM_DRAG_FPS` (default 24) already caps for drags.
3. **Cursor.** If the pointer is missing or lags on virtio, `WLR_NO_HARDWARE_CURSORS=1` is the known wlroots switch.

How to find out (about 20 minutes on the dev VM): in Proxmox set Hardware -> Display to VirtIO-GPU, restart the VM, run
`scripts/gpu-report.sh` (it prints the driver, whether the device has 3D, the Mesa renderer string and the renderer Fleetwm will pick),
log in, and run `scripts/grab-stats-run.sh build OUT --renderer pixman` and `--renderer gles2` on the same scene; repeat with VirGL GPU.
The numbers decide the default per GPU kind.

### What went wrong on the ThinkPad E490 (from the repository history)
The E490 (Intel UHD 620, Debian 13) is the real-hardware check of 2026-10-20. Problems found there and their fixes: the installer
enabled the Fleetwm login screen on a machine that already had a display manager (lightdm), which fought over the screen (fixed:
`ecca50f`, `1e1e0d4`); a session started by a display manager was given the idle seatd seat and drew nothing (`gbm_bo_create failed`;
fixed: the compositor asks logind for the seat when `XDG_SESSION_ID` is set, `57d7fe2`); power profiles never applied (`f9e091e`); a
Chromium crash in the seat keyboard (`fd05c2a`, argued from the wlroots source, never reproduced); and 1198 tests taking 13.2 s there against
0.88 s for 738 on the VM (not explained). Not known to this analysis: anything that went wrong on that laptop and is not in the commits or notes.
