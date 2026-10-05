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
| Exynos5422 (Mali-T628 MP6) | armhf | pixman (software) -- kernel has no Panfrost driver built in ([`CONFIG_DRM_PANFROST`](https://docs.mesa3d.org/drivers/panfrost.html) unset), so GLES2/EGL init fails and the compositor falls back automatically | Verified end-to-end on real hardware (2026-08-18): greeter, login, bar, wallpaper all confirmed working over the software renderer |
| Asus Tinker Board (Rockchip RK3288) | armv7l | untested -- build never completed | **Blocked, hardware issue, not a fleetwm bug** (2026-08-19): a full-parallelism `ninja` build (4 jobs) on this board's 2GB RAM caused it to drop off the network entirely mid-compile -- reproduced, not a one-off. This board's Micro-USB power input is a [documented Armbian undervoltage/brownout issue](https://www.hometutoring.co.nz/electronics/rpi_alt.php#:~:text=Armbian%20notes%3A%20%22Severe%20powering%20troubles%20due%20to%20Micro%20USB%20power%20connector.%20It%27s%20recommended%20to%20power%20through%20GPIO%20pins%20to%20prevent%20under%2Dvoltage%20issues%20(instabilities%2C%20boot/crash%20cycles).%20Powering%20situation%20is%20a%20little%20improved/masked%20on%20model%20S.%22), not something the compositor code can fix. If retrying: power via the GPIO pins instead of Micro-USB, and cap build parallelism (`ninja -j1`/`-j2`) to reduce peak draw. |
| Amlogic S922X (ODROID-N2, Mali-G52 MP4) | aarch64 | not yet confirmed which renderer is in use (GLES2/Panfrost vs. pixman fallback) | Verified end-to-end on real hardware (2026-08-19): full mandatory PGO install pipeline, 243/243 unit tests passing directly on-device, real login/desktop use with an external monitor attached |

Any GPU whose driver fails hardware EGL initialization (not just one
missing an extension) gets the same automatic pixman software-rendering
fallback the Exynos5422 board uses above -- see
`wlr_pixman_renderer_create()` in `src/compositor/server.cpp`. That
keeps the desktop usable, just without GPU acceleration, on hardware
like older/unsupported Mali Midgard GPUs.
