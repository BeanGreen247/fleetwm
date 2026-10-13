# Building on Debian 12, 13 and 14

Supported targets: Debian 12 (bookworm), 13 (trixie), 14 (forky). Debian 10 and 11 are not supported: they ship wayland 1.16/1.18, libdrm
2.4.97/2.4.104, meson 0.49/0.56 and wlroots 0.11 or none, so wayland, libdrm, libinput, meson and wlroots would all need building from source.

| Release | wlroots | How |
|---|---|---|
| Debian 14 (forky) | 0.20 from the archive (`libwlroots-0.20-dev`) | plain `install.sh` |
| Debian 13 (trixie) | 0.18 from the archive (`libwlroots-0.18-dev`) | plain `install.sh` |
| Debian 12 (bookworm) | 0.15 in the archive, too old | wayland >= 1.23, libdrm >= 2.4.122 and meson from `bookworm-backports`, wlroots 0.18.2 built from source into `/usr/local` (`scripts/build-wlroots-from-source.sh`); `install.sh` does both |

`meson.build` looks for `wlroots-0.20`, `wlroots-0.19`, `wlroots-0.18`, then `wlroots`. The three API differences between 0.18 and 0.19/0.20
are handled with `WLR_VERSION_NUM >= 0x001300` (scene output pending damage moved into `WLR_PRIVATE`, `wlr_xdg_surface_get_geometry` became the
`geometry` field, `wlr_xwayland_surface_set_maximized` takes horizontal and vertical flags), and `scripts/patch-wlroots-cpp-headers.py` also strips
`[static N]` from `wlr/render/color.h`.

## What was tested (2026-10-13, Docker, clean images, `-Dtests=true`)

| Image | Compiler | wlroots | Result |
|---|---|---|---|
| debian:bookworm + backports | gcc 12 | 0.18.2 from source | all 263 steps build, 22/22 unit tests pass |
| debian:forky | gcc 15 | 0.20.2 package | all 263 steps build, 22/22 unit tests pass |
| Debian 13 (this workstation) | gcc 14 | 0.18.2 package | builds, 22/22 unit tests pass |

wlroots 0.19 was only checked by reading its headers (debian:sid package); it was not compiled against. Nothing was run in a session on 12 or 14: the
compositor was built, not started. The `install.sh` Debian 12 branch was run only in pieces (the same apt and script commands inside the image),
not as a whole script.

## Fixes this needed

Missing includes only exposed by newer or older libstdc++ (`<unistd.h>` in `src/fleetfm/window_ops.cpp`, `<cstddef>` in `src/compositor/switcher.hpp`),
and the wlroots items above. The code stays C++20 (gcc 12 and later are fine). A `-std=c++17` build would fail only on 17 `operator==() = default`
and one `std::erase_if`.
