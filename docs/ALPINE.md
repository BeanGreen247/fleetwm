# Alpine Linux

Fleetwm builds and runs on Alpine Linux 3.21 or newer (musl libc, OpenRC, elogind, seatd). `./install-alpine.sh` does what `install.sh` does on Debian:
installs the packages with `apk`, builds into `build/`, installs to `/usr/local`, enables dbus, elogind, seatd and udev, adds you to the input, video,
render, audio and seat groups, and puts the Fleetwm login screen on tty1 through `/etc/inittab` (`packaging/fleetwm-greeter-run`, PAM service with
`pam_elogind` in `packaging/fleetwm-greeter-pam-alpine.conf`). `FLEETWM_NO_GREETER=1` skips the login screen; `FLEETWM_GREETER_TTY=tty2` moves it.

Tested (2026-10-26): a clean `alpine:3.21` container, run as root: the whole script, build and install, greeter and file-manager network helper
included (Samba support is left out, Alpine's wlroots is 0.18 which the code supports), and the unit tests: 1234 of 1234 pass with `bash` and GNU `tar`
installed (busybox `tar` has no `--sort`, which the settings backup uses; the script installs both). Not tested: a boot on real hardware or a VM
(inittab respawn, seatd, the login screen itself), PAM login through `pam_elogind`, sound through pipewire on OpenRC.

Differences from Debian: `mallopt` does not exist on musl (the tuning is skipped; musl's allocator has no per-thread arenas and gives memory back),
`renameat2`/`RENAME_NOREPLACE` go through the syscall, `dev_t` needs `<sys/types.h>`. Alpine's `mesa` links `libgbm`/`libEGL` against `libgallium`, which
links LLVM, so the compositor maps `libLLVM` (33 MB of file-backed pages, 6.6 MB of it written by musl's eager relocation) even on the pixman renderer
(`docs/OPTIMIZATIONS.md`, Alpine measurements).

## Low-memory build (software renderer only)
`FLEETWM_PIXMAN_ONLY=1 ./install-alpine.sh` builds wlroots 0.18 (`scripts/build-wlroots-from-source.sh`) with the pixman renderer only: no EGL, GBM, Mesa
or LLVM, no XWayland, no GPU drawing. Measured in `alpine:3.21`, headless, 1280x720, 20 s idle, compositor: **53.8 MB -> 12.3 MB PSS** (anonymous 8.3 -> 0.8,
file-backed 37.0 -> 3.0 MB), the whole stack of compositor, bar, wallpaper and desktop 65.3 -> 23.6 MB. The full script ran in a clean container with the
option on (greeter and network helper included). Not tested: real KMS output without the GBM allocator (wlroots then uses dumb buffers), so run it on a
spare machine first. The compositor code needs nothing else: `src/compositor/gles2_compat.hpp` makes the GLES2 checks compile without `<wlr/render/gles2.h>`,
and EGL/GLES2 are optional in `meson.build`.

