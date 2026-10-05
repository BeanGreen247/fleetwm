# Optimizations

What was measured, what changed, what was rejected, and the traps hit along
the way. Numbers come from the `fleetwm-dev` VM (Debian 13, 2 cores, 1280x800
virtio display) unless stated; "idle ticks" are CPU ticks (100 = 1 s) of a
standalone process over the stated window.

## Build and memory (earlier work)

- Release flags: `-Db_lto=true`, `-march=native`, `-ffunction-sections
  -fdata-sections -Wl,--gc-sections`, `-fno-semantic-interposition`,
  `-Db_ndebug=true -Dstrip=true`, full RELRO (`-Wl,-z,now`),
  `-DG_DISABLE_ASSERT`. Check the meson buildtype after every `meson setup`:
  a dev build silently drifted to `debugoptimized` once.
- Unity builds for the PGO pipeline (about 32% faster build). Unity merges a
  target's `.cpp` files into one translation unit, so duplicate
  anonymous-namespace helpers only fail there: share helpers in a header.
- PGO: `scripts/build-pgo.sh`, `scripts/build-pgo-auto.sh` (instrumented
  build, tests, synthetic training, optimized rebuild, tests again).
  Memory is unchanged by PGO; the benefit is CPU efficiency.
- glibc heap retention: `mallopt` pins the mmap/trim thresholds to 64 KB in
  every binary and jemalloc is preloaded (`LD_PRELOAD`) in the session, which
  returns memory to the exact baseline after terminal spawn bursts
  (62 MB reclaimed in the worst measured case).
- Zombie children are reaped via a SIGCHLD handler.
- The greeter forces `WLR_RENDERER=pixman` (it only draws a login card) so
  Mesa/EGL (about 36 MB) is never mapped into the long-lived greeter process.
- GTK clients that remain use `GSK_RENDERER=cairo` and `NO_AT_BRIDGE=1`.
- Declined on purpose: disabling XWayland, disabling PIE, `-Ofast`,
  `-fno-strict-aliasing`, software rendering for the real compositor, idle
  frame throttling (the compositor already measures 0 CPU ticks idle).

## GTK-free shell clients (fleetkit) (2026-10-05)

The always-resident and frequently launched clients were rewritten as plain
Wayland clients on the small `src/fleetkit` toolkit (layer-shell surfaces on
`wl_shm`, cairo drawing, xkbcommon input, `poll()` loop; nothing redraws unless
something visible changed). The compositor is unchanged for applications: GTK
2/3/4, Qt and XWayland apps keep working.

| client | libs old -> new | RSS old -> new | PSS old -> new | idle CPU old -> new |
|---|---|---|---|---|
| wallpaper | 71 -> 13 | 112 MB -> 5.2 MB | 69 -> 1.6 MB | 6 -> 0 ticks/10 s |
| locker | 74 -> 30 | 160 MB -> 13.5 MB | 108 -> 5.3 MB | 45 -> 0 |
| powermenu | 73 -> 29 | 156 MB -> 13.4 MB | 105 -> 5.3 MB | 7 -> 0 |
| launcher | 71 -> 27 | 149 MB -> 10.3 MB | 106 -> 3.9 MB | 28 -> 0; first window 63-79 ms -> 25-27 ms |
| bar | 72 -> 33 | 147 MB -> 12.1 MB | 105 -> 6.2 MB | 25 ticks/30 s -> 1 |
| audiomixer | GTK -> 28 | n/a -> 11 MB | | 0 |
| settings | 72 -> 28 | 75 MB -> 15 MB | 61 -> 5.9 MB | 0 -> 0 |
| greeter-login | GTK -> 30 | 62 MB -> 17 MB | | 0 |
| compositor | 99 -> 50 | | | |

Resident shell memory (bar + wallpaper) went from about 260 MB to about 17 MB.
`fleetwm-settings` and `fleetwm-greeter-login` were rewritten as well. fleetwm
no longer has any GTK, GLib or Pango dependency; the GTK-only helpers
(`app_style`, `clean_quit`, `BatterySource`) and the GTK CSS (`themes/base.css`,
`corners-*.css`, `accent.css`) were deleted. The theme `*.css` files are kept
only as the colour palette that fleetkit reads.

### Pieces of the toolkit

- `src/fleetkit/fleetkit.{hpp,cpp}`: `App` (connection, registry, seat, outputs,
  timers, extra fds, thread-safe `post()`, clipboard paste), `Surface`
  (layer surface + double-buffered shm + cairo, redraw coalescing, frame
  callbacks), `Tooltip`, theme `Palette` read from `themes/*.css`.
- `image.cpp` (libpng/libjpeg/libwebp), `svg.cpp` (vendored nanosvg, zlib
  licence), `icon_theme.cpp` (freedesktop icon theme lookup),
  `desktop_entry.cpp` (.desktop scanning, Exec expansion).
- `src/common` is split into `libcommon_core` (toml++ only) and `libcommon`
  (GTK/GLib helpers); the compositor and the fleetkit clients link only the core.
- The system tray is an sd-bus StatusNotifierWatcher + host in `src/bar`.

### The settings window

`fleetwm-settings` is an `xdg_toplevel` (app id `dev.fleetwm.Settings`, which
the compositor floats, centres and keeps on top) drawn through
`src/fleetkit/ui.{hpp,cpp}`, a small immediate-mode widget layer: tabs,
check boxes, radio groups, buttons, spin buttons (typing, arrows, wheel),
sliders, a colour picker (saturation/value square, hue bar, presets, hex
entry), a file chooser, scrolling, Tab/Shift+Tab focus and keyboard
activation. Widgets are functions called from the draw callback, so nothing is
retained or redrawn while idle. Default applications are read and written
through `src/fleetkit/mimeapps.cpp` (mimeapps.list / mimeinfo.cache, the same
files GIO uses). Every tab of the GTK version is kept: Theme (with the power
section), Bar, Wallpaper, Default Apps, Audio, Performance, About.

Trap: with immediate-mode widgets a click that opens or closes a dialog only
takes effect on the *next* frame, so the draw callback must queue another
frame when `Ui::wants_another_frame()` is set, otherwise the result sits
unused until the next input event.

### Bugs found and fixed on the way

- Default-sink binding race in the volume readout and mixer (they only bound
  the sink if the default-sink metadata arrived before the node): both now
  keep candidate sinks and bind when either side arrives, and rebind on change.
- Compositor: if the locker dies while locked it is respawned (never
  unlocked); max 5 respawns in 10 s.
- Compositor: a focused client received no keyboard `enter` when the seat had
  no keyboard device yet (hot-plug, VMs driven by virtual keyboards).
- This compositor re-sends layer-surface `configure` on every commit; a client
  that redraws on every configure loops forever. fleetkit redraws only when the
  size changed.
- libwayland aborts the process when an event arrives with a NULL listener:
  every listener slot is filled. Seat/output listeners must be attached
  before the next dispatch or their first events are discarded.

### Method and traps

- Measure standalone processes with identical method (VmRSS/PSS from `/proc`,
  CPU ticks over a window). `WAYLAND_DEBUG=client` shows redraw loops.
- Test on the VM through the greeter: `wtype`, `wlrctl`, `grim` against
  `/run/user/1000` (session) or `/run/fleetwm-greeter` (greeter, needs sudo).
  `wlrctl pointer move` is relative: pin to the corner first.
- Audio tests: `modprobe snd-dummy`, PipeWire + WirePlumber, and a stream with
  `pw-cat`. Test tray icons with a tiny sd-bus StatusNotifierItem client.

## Known gaps

- Volume readouts show PipeWire's linear value, so a sink `wpctl` reports as
  0.70 reads 34%.
- The island bar layout needs an output of at least 1366 px and was not
  visually verified on the 1280 px test VM.
