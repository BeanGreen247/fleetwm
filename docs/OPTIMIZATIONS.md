# Optimizations

What was measured, what changed, what was rejected, and the traps hit along
the way. Numbers come from the `fleetwm-dev` VM (Debian 13, 2 cores, 1280x800
virtio display) unless stated; "idle ticks" are CPU ticks (100 = 1 s) of a
standalone process over the stated window.

## How we decide: non-pessimization first

This is the rule for all code and performance work here. Premature optimization
means making code cleverer to make it faster. **Non-pessimization** is the
opposite and the first step: do not make the code do work it never needed to do.
Before tuning anything, remove what slows it down for no reason:

- work done every frame or every event that could be done once, or only when
  something changed (relayout, redraw, config reloads, IPC broadcasts);
- allocations, string building and copies in hot paths; pass by reference, reuse
  buffers, build text only when it will be shown;
- timers and wakeups that keep a process busy while nothing happens;
- polling where an event or a file watch already exists;
- layers, wrappers and abstractions that only forward a call;
- data laid out so the common case touches many cache lines (scattered pointers
  instead of one array);
- safety checks that are on in release builds only because nobody removed them.

Order of work: first remove waste (this costs nothing and never makes code
harder to read), then measure, then optimize only what the measurement points at.
Always measure with the same method before and after (see the notes below), and
record what was rejected so it is not tried again.

## Release build: speed over safety (2026-10-05)

The installer's build now trades security hardening for speed, on purpose.

- Hardening removed: stack protector, stack-clash protection, control-flow
  protection (`-fcf-protection=none`), `_FORTIFY_SOURCE`, full RELRO (now lazy
  binding and `-z norelro`), and PIE (`-fno-pie -no-pie`).
- Compiler: `-fno-plt`, `-fno-math-errno -fno-trapping-math`,
  `-fomit-frame-pointer`, and for the profile-use stage
  `-fprofile-partial-training` (the synthetic training run only covers part of
  the code, so code it never ran is still optimized for speed, not size).
- Linker: `--as-needed`, `-O1`, `--sort-common`, `-z noseparate-code` (fewer
  pages mapped).
- Compiler warnings and notes are switched off (`-w`, `warning_level=0`), and the
  build prints only a progress counter; the full output is in `build-pgo.log`
  and shown only when something fails. Unit tests print a summary only.
- The compositor runs at nice -10 (`setpriority`), allowed for the `video` group
  by `/etc/security/limits.d/fleetwm.conf` (PAM `pam_limits` in the greeter
  session). Every program it starts goes back to normal priority.
- Not used: `-ffast-math`, `-fno-exceptions`/`-fno-rtti` (toml++ and our code use
  exceptions), static libstdc++ (more memory per process).

## Build and memory (earlier work)

- Release flags: `-Db_lto=true`, `-march=native`, `-ffunction-sections
  -fdata-sections -Wl,--gc-sections`, `-fno-semantic-interposition`,
  `-Db_ndebug=true -Dstrip=true`, `-DG_DISABLE_ASSERT` (full RELRO was
  removed, see the section above). Check the meson buildtype after every `meson setup`:
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
- Fleetwm's own programs no longer use GTK. The session sets `NO_AT_BRIDGE=1` for GTK apps you run (no accessibility bus). It used to force `GSK_RENDERER=cairo` too; that was removed because it made GTK 4 apps skip the GPU.
- Declined on purpose: disabling XWayland, `-Ofast`,
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

## Where the remaining memory was (and what removed it)

A profile of the live session showed the rewrite was not the whole story:

- **The compositor was 148 MB on the GPU-less test VM**, of which 54 MB was
  `libLLVM` and 11 MB the gallium driver: with no GPU render node the GLES2
  renderer runs on Mesa's llvmpipe software rasterizer. The compositor now
  uses the pixman renderer when `/dev/dri/renderD*` does not exist (an
  explicit `WLR_RENDERER` still wins, and any machine with a render node keeps
  GLES2): 148 MB -> 19 MB.
- **The session-wide jemalloc preload cost about 11 MB per small client**
  (bar 25 MB with it, 14 MB without). It is only worth having in the
  compositor, so the compositor (and the greeter, for its login card) now drop
  `LD_PRELOAD`/`MALLOC_CONF` from the environment after start-up; the bar,
  wallpaper and everything the user launches run on plain glibc with the
  `mallopt` tuning.

Result on the VM (RSS): compositor 148 -> 19 MB, bar 25 -> 14 MB, wallpaper
9 -> 4 MB; the whole resident session is **38 MB**, against 182 MB after the
client rewrite alone and 280 MB originally.

## Idle wakeups and the look (2026-10-05)

- **Custom FPS cap kept the compositor busy forever.** In `render_mode =
  "custom"` the cap timer forced an (empty) frame request, whose page flip
  produced the next frame event, which re-armed the timer: an idle desktop
  committed about 150 frames a second (1493 DRM atomic commits in 10 s,
  1.5% CPU). The throttle path now only keeps the loop alive while the
  output has pending damage. Idle commits: 1493 -> 5 per 10 s, idle CPU
  1.5% -> 0%. Found with `strace -c -e trace=ioctl -p <pid>` and `perf`.
- The bar's clock and stats run on separate timers (clock aligned to the
  minute when seconds are hidden, stats every 2 s, redraw only on change),
  and seconds are off by default.
- Debug overlay (Alt+Shift+I): dark backing panel, a `CPU%` row for the
  compositor process, and the same frame-time graph, renderer name, FPS and
  RSS rows.
- Look: floating capsule bar (workspaces | clock | status, theme-following
  workspace pills, dim labels with bright values), a launcher with icons,
  soft shadow and key hints, a settings sidebar with switches and segmented
  controls, Inter as the UI font, 8 px window gaps by default. Direction
  taken from GNOME-style capsule bars and dark "glass" Hyprland setups for
  structure, with dwl/dwm's discipline for borders (thin, one accent).

## Known gaps

- Volume readouts show PipeWire's linear value, so a sink `wpctl` reports as
  0.70 reads 34%.
- The island bar layout needs an output of at least 1366 px and was not
  visually verified on the 1280 px test VM.

## Notes: rounded corners tried and dropped

- Rounded window corners via SceneFX 0.2.1 worked but clipped terminal text and the prompt, so it was removed.
  wlroots itself has no corner-radius API (0.18 through master). SceneFX also needs linking before wlroots
  (both export `wlr_scene_*`) and a GL renderer, which costs the pixman memory saving.
- The SIGCHLD reaper must not `waitpid(-1)`: it stole Xwayland's exit status from wlroots.
- Known gap: XWayland is started but X11 windows are not managed (no `new_xwayland_surface` listener).

## Desktop (floating) layout cost

- Titlebars are rendered with cairo into a CPU buffer only when the title, focus, width, maximized
  state or hovered button changes (`View::update_titlebar`, which compares without allocating since
  `resize_border` runs on every client commit). Idle cost is zero.
- cairo/fontconfig are only initialised once a titlebar is first drawn, so the Tiling layout does not
  pay for them. Measured on the dev VM: compositor Pss 12.7 MB with no windows, +1.7 MB for the cairo
  init in Desktop mode, about 1.5 MB per window including its titlebar buffer.
- Interactive move/resize maths lives in `src/common/window_geometry.*` (unit tested) so the grab code
  in `server.cpp` stays thin.

## Taskbar and window list

- The compositor publishes its window list to IPC clients that send `SUBSCRIBE_WINDOWS`
  (`WINDOWS\t<id>\t<flags>\t<app_id>\t<title>...`, parsed by `src/common/window_list.*`). Changes within one
  event-loop iteration are coalesced into one idle-callback broadcast, and identical snapshots are not re-sent,
  so a busy terminal changing its title does not flood the bar. Only subscribed clients receive it.
- The bar redraws on a changed snapshot only; icons are looked up lazily and cached per app id, and the desktop
  entries are scanned once on the first window.
- The start menu is `fleetwm-launcher --start-menu --edge <side> --inset <px>`: a full-output transparent
  overlay with the card placed beside the taskbar, which is what makes click-away and re-clicking the start
  button close it without any extra IPC.

