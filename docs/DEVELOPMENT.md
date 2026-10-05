# Building and development

Quick way to run the unit tests: `scripts/test.sh` (see `scripts/test.sh --help`). The older
`scripts/run-tests.sh` still works too.

## Building from source

```sh
meson setup build --prefix=/usr/local --buildtype=release
ninja -C build
sudo ninja -C build install
```

`install.sh` builds with a heavier set of flags than the plain command
above -- LTO, `-march=native`, dead-code stripping, and more (see its own
comments for what and why) -- **and now always builds with profile-guided
optimization (PGO) on top**, via `scripts/build-pgo-auto.sh`: an
instrumented build, a synthetic training pass
(`scripts/pgo-train-session.sh`, driving bar/wallpaper/settings/
launcher/powermenu/audiomixer against the compositor under wlroots'
headless backend, no live usage session required), then a final
profile-optimized rebuild. This is mandatory, not opt-in -- every
`install.sh` run pays the extra build time (roughly a minute or two more
than a plain build) for a profile-guided binary, not just installs where
someone happened to run PGO by hand. `scripts/build-pgo.sh` (which
`build-pgo-auto.sh` wraps) still works standalone too, for a real
live-usage training session instead of the synthetic one -- see that
script's header comment for the manual `generate`/`use` steps.

A `tests/` unit-test suite (GTest, fetched via wrapdb) covers the
config-parsing modules in `src/common/` plus the pure PipeWire-JSON-field
and IPC-socket-path helpers the bar/audio mixer rely on. `install.sh`
(via `build-pgo-auto.sh`/`build-pgo.sh`) runs `meson test` automatically
right after every build (both the instrumented and final PGO stages) and
before install, so a failing test blocks the install rather than
shipping silently broken.

To just build and run the tests on their own, without a full install,
`scripts/run-tests.sh` does it in one step (a throwaway `build-tests/`
directory, left alongside `build/` rather than reusing it):

```sh
$ scripts/run-tests.sh
...
[==========] Running 267 tests from 14 test suites.
[----------] Global test environment set-up.
[----------] 20 tests from ParseHexColor
[ RUN      ] ParseHexColor.ValidLowercase
[       OK ] ParseHexColor.ValidLowercase (0 ms)
...
[----------] 9 tests from IpcClientWithServerTest
[ RUN      ] IpcClientWithServerTest.ConnectSucceedsWhenServerIsListening
[       OK ] IpcClientWithServerTest.ConnectSucceedsWhenServerIsListening (0 ms)
...
[==========] 267 tests from 14 test suites ran. (140 ms total)
[  PASSED  ] 267 tests.
```

That unit suite is deliberately narrow (no compositor, Wayland session,
or wlroots scene tree -- see `tests/meson.build`'s own comment). A
separate, slower layer covers the compositor itself:
`scripts/smoke-test.sh` boots the real compositor under wlroots'
headless backend (the same pattern the PGO training pass below already
uses) and drives it over a real IPC socket -- workspace switching,
out-of-range/malformed-command handling, and a clean SIGTERM shutdown:

```sh
$ ninja -C build
$ scripts/smoke-test.sh build
...
==> 10 passed, 0 failed
```

fleetwm's own clients no longer use GTK at all (see
[GTK-free shell clients](#gtk-free-shell-clients-fleetkit)), which is where
most of the memory went: a typical GTK client pulls in GLib, Pango, GdkPixbuf and
(with the GL renderer) Mesa's full GL/EGL/gallium stack, 100-160 MB resident
per process. Measured on the test VM, the bar went from 147 MB to 12 MB.

### Memory footprint and allocator tuning

Every fleetwm binary (the compositor and every client) also:

- Reaps its own spawned children via a `SIGCHLD` handler
  (`server.cpp`) -- every terminal/app launch used to leave a
  `<defunct>` zombie behind once it exited.
- Pins glibc's `mallopt(M_TRIM_THRESHOLD/M_MMAP_THRESHOLD)` to a fixed
  64KB (`src/common/malloc_tuning.cpp`) instead of glibc's default
  fully-dynamic thresholds, which only ever grow and stop returning
  freed memory to the OS.
- Prefers **jemalloc** over plain glibc malloc when
  `libjemalloc2` is installed (`LD_PRELOAD`, set in
  `src/greeter/session.cpp`'s `build_env()` and
  `packaging/fleetwm-greeter@.service`; degrades cleanly to the tuned
  glibc above if the package isn't present), with its background purge
  thread enabled (`MALLOC_CONF=background_thread:true,dirty_decay_ms:
  5000,muzzy_decay_ms:5000`) so freed memory gets returned to the OS on
  a timer even while the process sits idle, not only as a side effect
  of a later allocation.
- Sets `NO_AT_BRIDGE=1` in the session so GTK applications you run do not
  activate the AT-SPI accessibility bus on startup (remove it from
  `src/greeter/session.cpp` if you need a screen reader).

The graphical greeter (`fleetwm-greet`) additionally forces
`WLR_RENDERER=pixman` -- it only ever draws a static login card, no
GPU compositing need at all, so this keeps Mesa/EGL/GLES2's driver
stack (`libLLVM`, `libgallium`, ~36MB by itself) from ever loading into
that process. This matters for the whole length of your session, not
just while the login screen is on screen: `fleetwm-greet`'s own process
forks into your authenticated session and then blocks in `waitpid()`
until you log out, so whatever it mapped while showing the login screen
stays resident the entire time you're logged in. Measured effect on a
real box: `fleetwm-greet`'s idle Pss dropped from ~103MB to ~15MB.

On a machine with no GPU render node the compositor picks the pixman
renderer on its own (no libLLVM, about 65 MB less resident). If the real
desktop compositor still falls back to Mesa's llvmpipe software
rasterizer (no real GPU render node present -- check for a
`renderD*` device in `/dev/dri/`, not just a `card*` one, which can
exist for display-only KMS with no actual render capability behind
it), `LP_NUM_THREADS` caps how many worker threads llvmpipe spawns
(defaults to one per CPU core, each holding its own JIT-compiled
shader copy) -- worth capping lower on a many-core machine that's
falling back to software rendering; not worth touching on a low-core
one, where the default is already small.

Release builds turn off hardening for speed and silence compiler warnings; see
[OPTIMIZATIONS.md](OPTIMIZATIONS.md) for the flags and the reasoning, including the
non-pessimization rule we follow for performance work.

Build dependencies (apt package names):

```
build-essential meson ninja-build pkg-config
libwlroots-0.18-dev wayland-protocols libwayland-dev
libinput-dev libdrm-dev libxkbcommon-dev libpixman-1-dev
libegl1-mesa-dev libgles2-mesa-dev
libgl1-mesa-dri libegl-mesa0 libgbm1 libvulkan1 mesa-vulkan-drivers mesa-va-drivers vulkan-tools vainfo
intel-media-va-driver i965-va-driver   (Intel GPUs only; picked from /sys/class/drm)
libcairo2-dev libpng-dev libjpeg-dev libwebp-dev fonts-dejavu-core
libpipewire-0.3-dev pipewire pipewire-bin wireplumber
libpam0g-dev
libjemalloc2
libsystemd-dev
polkitd pkexec
xwayland
foot
```

`polkitd`/`pkexec` are a runtime, not build, dependency -- the power
menu's Sleep/Reboot/Shut down go through `systemctl`, which
`systemd-logind` refuses to authorize for a non-root caller without
polkit running, regardless of session state. `libsystemd-dev` is a real
build dependency (`sd_pid_get_session()`, used to resolve the session
id for Log out without depending on `$XDG_SESSION_ID` being set, which
modern `pam_systemd` no longer guarantees).

Build with `-Dxwayland=false` to disable XWayland support and drop the
`libxcb-dev` dependency. Build with `-Dgreeter=false` to skip
`fleetwm-greet` and its `libpam` dependency. If you enable the greeter
manually (not via `install.sh`), also install and enable `seatd`
(`apt install seatd && systemctl enable --now seatd.service`) -- see
[Greeter](GREETER.md).

## Architecture

Ten processes, communicating over a Unix domain socket and a
signal/pidfile mechanism -- see [adr](adr) for the reasoning
behind each:

- **`fleetwm`** -- the wlroots-based compositor and window manager
- **`fleetwm-bar`** -- the always-resident top bar (GTK-free, see
  [GTK-free shell clients](#gtk-free-shell-clients-fleetkit))
- **`fleetwm-settings`** -- the settings app, spawned on demand (GTK-free)
- **`fleetwm-launcher`** -- the app launcher popup, spawned on demand
  (`Alt+D`), exits after one launch/dismiss
- **`fleetwm-wallpaper`** -- the background renderer, autostarted with the
  compositor
- **`fleetwm-powermenu`** -- the power menu (Lock/Log out/Sleep/Reboot/
  Shut down), spawned on demand from the bar's power icon; a standalone
  fullscreen layer-shell overlay, for reliable click handling -- exits
  after one action or a dismiss
- **`fleetwm-locker`** -- the lock screen, spawned on demand (`Alt+Shift+L`
  or the power menu's Lock); PAM-verifies the password in-process and
  signals the compositor to unlock over the IPC socket
- **`fleetwm-audiomixer`** -- the audio mixer popup (master + per-app
  volume sliders), spawned on demand from the bar's volume stat; same
  standalone layer-shell-overlay approach as `fleetwm-powermenu` -- exits
  on dismiss
- **`fleetwm-update`** -- the update script
- **`fleetwm-greet`** -- the optional graphical login greeter, an
  alternative to running a full display manager (see
  [Greeter](GREETER.md)); not part of the IPC socket/signal mechanism since
  it runs before any session exists -- it's a small wlroots compositor of
  its own that hands off to `fleetwm` on a successful login
- **`fleetwm-greeter-login`** -- the login-screen UI (GTK-free)
  `fleetwm-greet` spawns and talks to over a private socket; never runs
  outside of a `fleetwm-greet` session

## Display management

Resolution, refresh rate and the position of each monitor in the desktop
layout are set from the **Display** tab of `fleetwm-settings` (drag the
screens in the arrangement view and they snap to each other's edges and
centres, or use the Place/Align controls). A changed mode asks for
confirmation and reverts by itself after 15 seconds; the compositor also
reverts on its own if the monitor cannot present the new mode. Settings are
stored per output name in `~/.config/fleetwm/outputs.toml` and applied when
the monitor appears:

```toml
[outputs."DP-1"]
width = 2560
height = 1440
refresh_mhz = 144000
x = 0
y = 0
```

The same operations are available over the compositor's IPC socket
(`OUTPUTS?` lists monitors and modes, `OUTPUT_SET <name> <w> <h> <refresh_mhz>
<x> <y>` applies and saves; use `0 0 0` to keep the mode and `-999999
-999999` to keep the position). The bar follows resolution changes live,
including switching between the full-width and island layouts.

## GTK-free shell clients (fleetkit)

The shell pieces -- `fleetwm-bar`,
`fleetwm-wallpaper`, `fleetwm-launcher`, `fleetwm-locker`,
`fleetwm-powermenu`, `fleetwm-audiomixer`, `fleetwm-settings` and
`fleetwm-greeter-login` -- are plain Wayland clients
(layer-shell surfaces on `wl_shm`, drawn with cairo) built on the small
`src/fleetkit` toolkit: one `wl_display` connection and `poll()` loop, xkbcommon
keyboard input with key repeat, pointer input, timers, a freedesktop icon
theme lookup (PNG via libpng, SVG via the vendored nanosvg), a `.desktop`
file scanner, an immediate-mode widget layer (tabs, spin buttons, sliders, colour
picker, file chooser) for the settings window, and the theme palette read from `themes/*.css`. They never
link GTK, GLib or Pango; the system tray speaks the StatusNotifierItem
protocol over sd-bus. Nothing redraws unless something visible changed, so
idle CPU is effectively zero and each process stays in the low tens of MB
instead of well over 100 MB.

This only concerns fleetwm's own clients. The compositor still serves
GTK 2/3/4, Qt, XWayland and any other Wayland or X11 application exactly as
before. fleetwm itself has no GTK dependency left. Measurements, the method, and the bugs found while
doing this are written up in [OPTIMIZATIONS.md](OPTIMIZATIONS.md).
