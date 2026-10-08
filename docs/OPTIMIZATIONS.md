# Optimizations

What was measured, what changed, what was rejected, and the traps hit along
the way. Numbers come from the `fleetwm-dev` VM (Debian 13, 2 cores, 1280x800
virtio display) unless stated; "idle ticks" are CPU ticks (100 = 1 s) of a
standalone process over the stated window.

Findings from the Lestrix speed round (method, hardware limits, governor, kernel pty and pipe tests, closed doors, and the checklist for the next Fleetwm round) are merged in `PERFORMANCE_FINDINGS.md`.

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

## Glass (Windows Aero) performance at a glance

Everything done to make the glass look cheap, in one place. Laptop numbers are from the Celeron N4020 test
laptop; microsecond numbers are from a build machine (a Celeron takes several times longer, the ratios hold).

| what | before | after | how |
|---|---|---|---|
| Bar with a seconds clock, glass on | 0.72% CPU | 0.28% CPU (glass off: 0.27%) | clock tick repaints only its strip; glass rectangles drawn once and copied |
| A glass rectangle (bar, taskbar, start menu, Alt+Tab panel) | clip, stretched backdrop copy, two gradients, two strokes per redraw | one copy | tile cache, 3 MB LRU, keyed by geometry, style and backdrop |
| Blurred wallpaper for glass | PNG read and decoded at every Alt+Tab step | decoded once per program, re-checked every 2 s | shared surface in `load_backdrop()` |
| Titlebar redraw on hover, glass on (800 px) | 342 us | 51 us | glass background and caption strip cached per state |
| Titlebar glass background alone | 103 us (800 px), 242 us (1600 px) | 4 us, 9 us | `titlebar_draw.cpp` background cache, 2 MB LRU |
| Titlebar caption strip | about 290 us | a copy | strip cache, 1 MB LRU, one picture per button state |
| Island bar with a ticking clock | layout re-sent every second (and a hover tooltip dropped) | only when the width changes | `island_resize_if_needed()` |
| Start menu with glass | full-output surface, +3.5 MB compositor RSS while open | card-sized surface, +0.9 MB | compositor closes it on an outside click |

Tests that keep it this way: `GlassCache` (cached tile equals a direct paint, styles never share a tile),
`TitlebarDraw` (cached background and strip equal the drawn ones in all 40 button states, caches stay within their
budgets, warm is clearly cheaper than cold, glass and matte differ only in the background), `CaptionButtons`.
Glass versus flat on the compositor: no measurable difference at idle (0.20% against 0.25% on the laptop).

## Overlay, glass, bar and start menu pass (2026-10-06)

Measured on the real testing-bed laptop (Celeron N4020, Intel UHD 600, 1920x1080, Debian 13) unless
stated. CPU is a percentage of one core. "Nested" means a throwaway compositor started inside the live
session, so the live desktop was never touched.

| change | before | after |
|---|---|---|
| Bar with a seconds clock, glass on | 0.72% | 0.28% |
| Bar with a seconds clock, glass off | 0.42% | 0.27% |
| Performance overlay on, desktop idle (compositor) | 58% | 0.27% |
| Performance overlay on, busy full-window terminal | 37.8% | 6.5% (6% with it off) |
| One overlay repaint | 1.0-1.2 ms | 0.5-0.6 ms |
| Start menu, compositor RSS while open (nested) | +3.5 MB | +0.9 MB |
| Start menu, launcher RSS / PSS | 14.2 / 5.2 MB | 12.0 / 4.2 MB |

What changed, in the order it was found:

- **Clock ticks repaint a strip.** `Surface::queue_draw_rect` (fleetkit) redraws and damages only a
  rectangle; the rest of the picture is copied from the last frame. The bar uses it for the seconds
  clock when the text width did not change, and falls back to a full redraw otherwise. Before, every
  second repainted the whole bar including the glass, and told the compositor the whole bar had changed.
- **Island bar re-sends its layout only when the width changes.** It used to set size, margins and
  anchor and drop any hover tooltip on every clock tick.
- **Glass is painted once per rectangle.** `paint_glass` keeps a small LRU of finished tiles (3 MB cap,
  keyed by geometry, style and backdrop). A redraw is one blit instead of a clip, a stretched backdrop
  copy, two gradients and two strokes. Tests compare a cached tile with a direct paint.
- **The blurred backdrop is decoded once per process** and re-checked against the wallpaper every two
  seconds, instead of reading and decoding the PNG on every Alt+Tab step.
- **The start menu surface is the card, not the whole output.** It needs far smaller buffers. The
  compositor closes the menu on a press outside it (layer namespace `fleetwm-start-menu`) and swallows
  that press, as the full-output surface did.
- **The performance overlay is one picture.** It was about 665 scene rectangles (64 graph bars, five
  text rows of 8 characters at 15 cells each, a panel). Every bar was resized every frame, and the
  update ran after the commit, so the overlay damaged the screen, caused the next frame, and kept the
  compositor rendering 60 frames a second by itself. Now: one cairo-drawn scene buffer, repainted at
  most four times a second inside the frame being committed (no frame of its own), one extra repaint
  when the desktop goes idle (to show 0 FPS), nothing allocated while it is off. A repaint copies a
  cached panel, writes graph bars straight into the pixels, reads CPU with one `getrusage` call, and
  reads memory and clock speed once a second. It shows FPS, frame interval, what the compositor itself
  spent per frame (build + commit), late frames, CPU, current memory and a graph.
- **dmabuf scanout feedback.** The compositor keeps the linux-dmabuf object and hands it to the scene
  (`wlr_scene_set_linux_dmabuf_v1`), so GPU clients are told which buffer formats the display can scan
  out. Without it a fullscreen video or game is always composited. wp_viewporter and
  wp_single_pixel_buffer are enabled too (scaling and solid colours without client-side work). Checked
  in a nested compositor: a Vulkan client negotiates dmabuf feedback with the new tranches. Not yet
  checked: an actual direct scan-out on the laptop's display, which needs the new build in the live
  session.

### Where the time goes now

A full-window terminal that scrolls constantly costs the compositor about 1.1 ms of CPU per frame on
the Celeron (6.5% at 60 fps; the overlay shows 0.3 ms of that as build + commit). About a third of the
profile is one `rep movsb`: Mesa copying the client's shm pixels into GPU memory. That is inherent to
shm clients; only GPU-buffer clients or smaller damage avoid it. Typing and small updates cost far less
because only the damaged area is uploaded. GPU time itself was not measured (it needs `intel_gpu_top`).

### GPU time, IPC and threads (2026-10-06, same laptop)

`intel_gpu_top` against a nested compositor with the new overlay on (the live session sat idle next to it):

| workload | render engine busy | GPU asleep (RC6) |
|---|---|---|
| desktop idle, overlay on | 0.7% | 98.4% |
| full-window terminal scrolling (`yes`) | 29.6% | 56.9% |
| `vkcube` (a GPU client) | 30.9% | 60.8% |

So a constantly scrolling full-window terminal needs about 5 ms of GPU per 16.7 ms frame, with the GPU
clocked near its minimum (it averaged about 170 MHz while awake; the maximum is 650). That is about three
times the headroom before frames could be missed, and the clock can still rise, so 60 fps is not at risk
on this hardware. The CPU side is 1.1 ms per frame (see above).

- **IPC** (`fleetwm.sock`, one text line per request): a `WORKSPACE?` round trip is 37 us median, 50 us
  at p99, 168 us worst over 2000 requests. Nothing to gain; it was not changed.
- **Threads.** The compositor loop is single-threaded because wlroots and the Wayland server library are
  not thread-safe; offloading rendering would mean a redesign. The threads that do exist are Mesa's
  (shader compile and the on-disk shader cache) and jemalloc's background purge (6 wakeups in 30 s).
  The bar has a PipeWire thread for the volume and one short-lived thread for `nvidia-smi`.
  `mesa_glthread=true` (GL driver work on its own thread) saved about 5% on a busy terminal and was left
  off. On a 2-core Celeron more threads mostly add context switches.
- **Wakeups at idle** (nested): compositor 1.0 context switches a second, wallpaper 0, bar 4.5 with a
  seconds clock. The bar's extra wakeups are the protocol round trip after each repaint (frame callback,
  buffer release), about three per repaint at 1.5 repaints a second.

### More ideas from other compositors (not applied)

- **niri**: after a redraw with no damage it waits for an estimated vblank before sending frame
  callbacks, so a client that redraws without visible change cannot spin. wlroots already behaves like
  this here (a commit with no visible damage schedules no frame, so no `frame_done` goes out), so this is
  covered.
- **gamescope**: nested micro-compositor with frame pacing and FSR upscaling for games; the lever
  relevant here is direct scan-out, which is now enabled (see above).
- **cosmic-comp (Smithay)** and **labwc/dwl/river**: the same damage-tracking and scan-out ideas; labwc
  hands the pipeline to wlroots, as Fleetwm does.
- **System level** (documented, not applied, because they change the machine and not the desktop):
  zram or zswap instead of disk swap on small-RAM machines (the laptop has 1.7 GB of disk swap at
  swappiness 60 and used 0 of it), the `performance` or EPP governor when latency matters more than
  battery, the i915 `enable_fbc`/`enable_psr` options on newer GPUs (not exposed on this kernel).

### Glass (Windows Aero) titlebars (2026-10-06)

Microbenchmark of `kit::draw_titlebar` (800 px wide, 32 px high, glass on, the pointer moving across the
buttons so every redraw is a hover change; build machine, so a Celeron takes several times longer):

| | before | after |
|---|---|---|
| whole redraw | 342 us | 51 us |
| glass background alone, 800 px | 103 us | 4 us |
| glass background alone, 1600 px | 242 us | 9 us |
| caption strip | about 290 us | a copy |

The strip (gradients, bevels, outlined glyphs, the hover glow) was most of a redraw and the glass background
the rest of the gap to matte. Both are drawn once per state (size, focus, colour; which button is lit,
maximized, pinned) into small byte-capped LRU caches and copied, so glass costs the same as matte. What
remains is the title text (about 30 us). `tests/test_titlebar_draw.cpp` checks the cached picture equals the
drawn one in all 40 button states, the caches stay within their budgets, and a warm redraw is under 60% of
a cold one.

### Profile-guided build training (2026-10-06)

The training run (`scripts/pgo-train-session.sh`, driven by `scripts/build-pgo-auto.sh`) used to start the
desktop programs, switch workspaces four times and wait 20 s, so most of the code the final binaries are
optimized for never ran. (Later widened and shortened again, see the 2026-10-07 section below.) It then ran 150 s on the virtual screen: six rounds, each a different layout, glass
and colour combination, each with every kind of work (workspace sweeps, four rotating Settings pages so all
twelve are drawn, shortcuts and language windows, start menu and launcher search, power menu, Alt+Tab,
snap keys, overlay, keyboard layout switching, idle inhibit, window queries, live layout flips with windows
open, terminals scrolling output, screen capture, pointer sweeps). Steps that need wtype, wlrctl, foot or
grim are skipped when they are missing. `tests/test_pgo_training.cpp` checks the length and the coverage.
Programs the desktop starts by name (the compositor autostarts the bar, wallpaper and padlock; keys and
menus start the launcher, Settings and the power menu) resolve through a `PATH` shim
(`scripts/pgo-path-shim.sh`) to the instrumented copies, so they are profiled even on a machine that has an
older Fleetwm installed, and nothing runs twice. The extra install time is about two and a half minutes.

### Training run: wider and shorter (2026-10-07)

Measured with the instrumented binaries built in a Debian 13 container and `gcov-dump` on the `.gcda` files
(functions with a non-zero arc count, and arcs with a non-zero count, over the whole build):

| | before (150 s, 5 rounds) | after (85 s, 6 rounds) |
|---|---|---|
| wall time of the training stage | 152 s | 87 s |
| functions run / arcs run, all programs | 33.5% / 23.4% | 40.1% / 29.1% |
| compositor `fleetwm` | 46.7% / 30.9% | 60.5% / 43.1% |
| `fleetwm-bar` | 67.6% / 37.3% | 80.7% / 49.9% |
| `fleetwm-settings` | 64.9% / 37.1% | 71.4% / 40.1% |
| shortcuts, mixer, launcher | 78.0, 73.7, 74.4% of functions | 87.0, 82.0, 67.9% (launcher: one run less) |
| kit (drawing toolkit), image decoders | 53.0% and 2.0% of functions | 64.3% and 56.8% |
| network code (`netmgr`) | 6.3% | 13.4% (the rest needs NetworkManager or wpa_supplicant) |

What was found and changed:

- **The keyboard chords never worked.** `wtype -P Super_L` presses the key but leaves the modifier state empty, so
  Super+Left, Alt+Tab, Alt+F10, the overlay keys and the workspace keys did nothing in every earlier training run.
  They now use `-M` and `-m`; snapping, maximizing and the workspace keys really run.
- **A plain `kill` killed Settings and the bar without a clean exit when PipeWire was running** (exit status 143,
  no profile written, no clean shutdown). The signals were blocked after PipeWire's thread had started, so the kernel
  could hand SIGTERM to that thread. `block_quit_signals()` (`src/common/quit_signals.hpp`) is now the first call of
  `main()` in every program that reads SIGTERM from a signalfd (ten programs), with a unit test for the mask and one
  that checks every `main()`.
- **New coverage**: every icon in every state (extra bars with fake battery and network trees, several at once; volume
  from a PipeWire null sink; the power-mode gauge through `power_mode` in `bar.toml`), all five themes, square and
  rounded corners, frame widths 0, 6, 8, 10 and 12 px, the three bar layouts, the taskbar on all four edges, window
  drags, resizes by every edge and corner, snaps by key and by dragging to the screen edges, maximize and minimize by
  button and double click (a small virtual pointer, `scripts/pgo-pointer.c`, built on the fly), tooltips and clicks on
  the taskbar, windows on several workspaces with send, next and previous, the Tiling layout's focus, pinned and
  floating borders, and a switch to 1024x768 and back.
- **Faster**: fixed sleeps replaced by waiting for the windows to exist, short settles, the look-independent phases
  (workspaces, menus, IPC queries) run in the first rounds only, Settings pages are spread over three visits, and the
  pointer phases run where the geometry is known. Coverage was the same at 60 s as at 120 s (28.9% against 29.1% of
  arcs), so the default fell from 150 s to 85 s, the time six rounds need.

### Glass against flat, measured again (2026-10-07)

On the Celeron laptop in a nested compositor, with the same pointer workload (Settings window dragged by its
titlebar and resized by its edge, 12 and 8 rounds), compositor plus bar CPU was 591, 611 and 619 ms with glass
against 605, 601 and 621 ms flat, and the render engine of the UHD 600 was busy 44.6%, 45.1% and 44.8% with glass
against 45.1%, 45.2% and 45.1% flat (`perflog/2026-10-07/glassbench.sh`, `glassgpu.sh`, raw in 15 and 16). Glass
costs nothing measurable beyond flat in motion; the titlebar, frame, bar and menu pictures are all cached, so there
is no glass-only work left to remove. A further "faster glass" change was not made because there is no gap to close.

### Mouse pointer (2026-10-07)

The pointer is drawn by `src/fleetkit/cursor_draw.cpp` (14 shapes, Aero with shadow in glass mode, flat otherwise)
and each (shape, style, scale) picture is made once and kept by the compositor (`kit::CursorCache`), the whole set
being under 0.7 MB. The theme's cursors are only used for names it does not draw. Mouse speed and pointer precision
go straight to libinput (`mouse.toml`), so there is no per-motion work in the compositor for them.

### Other speed-ups, by commit (so none is lost)

Performance work that is not in a table above, from the git history and this session:

- **Window border colours parsed once per theme load** (`1776b66`): they used to be parsed again on every client
  commit (60 or more times a second for a busy window).
- **Cursor name cached** (`9ea090b`) instead of looked up on every pointer motion.
- **Leftover debug logging removed** (`aea23d5`): `wlr_log_init(WLR_DEBUG)` logged every cursor motion and every
  scene commit, and a raw `fprintf` logged every key press.
- **FPS cap loop** (`c6b03c8`): Custom render mode committed empty frames about 150 times a second on an idle
  desktop; the loop now ends when nothing is damaged (see the section on the cap above).
- **Bar timers** (`c6b03c8`): the clock and the CPU/GPU stats run on separate timers, and stats redraw only when
  the text changed.
- **Pixman renderer without a GPU render node** (`3486a5a`), **jemalloc kept to the compositor** (`3486a5a`),
  **zombie children reaped, allocator tuned, the login screen without a GPU stack** (`34f6316`).
- **Release flags, LTO, `-march=native`, unity builds, PGO** (`a06525c`, `686e9fc`, `3a5eac8`; see the build
  sections), now with the wider 150 s training run and the `PATH` shim (`scripts/pgo-path-shim.sh`) that makes
  the programs the desktop starts by name the instrumented ones.
- **Static check for source-file name clashes before the build** (`scripts/check-unity-collisions.py`, run by
  `install.sh` and `scripts/test.sh`; `scripts/check-unity.sh` is the full unity build): the installer's unity
  build merges files, and a clash used to cost minutes of compiling before it failed.
- **Volume mixer: single instance and closing on an outside click** (`src/common/single_instance.hpp`,
  `src/common/popup_namespaces.hpp`): a second launch closes the open mixer instead of starting another
  process; dozens of idle mixer processes (each a PipeWire client) used to pile up on the test laptop. It also
  opens next to the bar or taskbar it came from (`src/common/popup_spot.hpp`).

### What other compositors do, and where Fleetwm stands

Sources: the KWin, Mutter, sway, Hyprland and wlroots documentation and blogs read while writing this.

| technique | who | Fleetwm |
|---|---|---|
| Draw only damaged regions, render nothing when idle | all (Hyprland calls it vfr) | yes: scene damage tracking, 0 idle frames |
| Direct scan-out of fullscreen GPU buffers | sway, KWin, Hyprland, Mutter | enabled, needs live check (see above) |
| Hardware cursor plane | all | wlroots default |
| Delay rendering until just before the deadline (sway `max_render_time`, KWin render-time prediction) | sway, KWin | not done: lowers latency, not CPU; would need frame-time prediction |
| Dynamic triple buffering when a frame runs late (also lets the driver raise GPU clocks) | Mutter, KWin | not done: matters on slow Intel parts that miss 60 fps, which this laptop does not |
| Real-time or raised priority for the compositor | Mutter (rtkit), KWin | nice -10 |
| Fixed thread-free small clients | none do this as far as read | fleetkit clients, 5-15 MB each |
| Overlay planes for video | KWin 6.x | wlroots decides; no code of ours |

### wlroots version

Debian 13 (the target) ships wlroots 0.18.2; Debian testing and unstable have 0.20.2, so `apt` on the
target cannot pull a newer one without adding testing, which would drag in newer system libraries.
Building 0.20 from source as a subproject is possible, but the code would need porting to the changed
API, and the 0.19 and 0.20 release notes list protocols (colour management, workspaces, capture) and
the Vulkan renderer, not anything for the GLES2 path this laptop uses. Rejected until a measurement
says otherwise.

### Tried and rejected

- `MESA_NO_ERROR=1`: 5.93% against 5.93-6.00% on a busy terminal, no effect.
- `mesa_glthread=true`: about 5% less total CPU on a busy terminal (nested only); not enabled.
- Paging out Mesa's idle libLLVM mapping (36 MB of the compositor's 89 MB RSS): those are clean page
  cache pages, `free` does not count them as used, and the kernel skipped them anyway.
- Caching or prewarming app start-up: the start menu, launcher, Settings, shortcuts, audio mixer and
  power menu each use 20-40 ms of CPU to start and 11-14 MB, so a resident copy would only cost memory.
- Per-commit work in `View::resize_border`: one small vector allocation per client commit, well under
  0.01% CPU; not worth touching.

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


## Round 2026-10-08 (section 8 test list; details in `PERFORMANCE_FINDINGS.md` section 9)

Rig: dev VM, KVM, 2 vCPU i5-8500, pixman, no governor exposed, perf_event_paranoid raised 3 -> 1 at runtime with approval.

- **Kept: line buffers in `IpcServer` and `IpcClient` use a head offset** instead of `erase(0, pos+1)` per line. 27-byte lines, `g++ -O2`, median of 5: 4 KB 7 us vs 4 us, 256 KB 21.9 ms vs 0.23 ms, 1 MB 503 ms vs 0.92 ms (quadratic before). Normal traffic unchanged. Bench: `perflog/2026-10-08/q.cpp`.
- **No change: splice/clipboard/screenshot paths** (row 7). The clipboard is wlroots passing fds, there is no screenshot code, the wallpaper reads one file.
- **No change: idle and scroll profile on a GPU-less VM** (row 13). Flat; no symbol above 4% while scrolling. Idle compositor + bar 0.012% CPU, about 3 context switches/s.
- **Rejected: align the bar's stats/disk/network/battery timers to one tick** (row 4). Upper bound = remove all four: bar 2 ms vs 9-11 ms CPU per 60 s (6 interleaved reps, VM i5-8500, `show_seconds=false`), wakeups/s 0.62 vs 1.12. The real change would recover part of 7 ms/min. The wpctl fallback (`popen` every 5 s) spawns a process and was not counted; it is a fallback only.
- **Rejected: "unchanged since last picture" flag for bar, titlebars and menus** (row 2). Titlebar state compare, frame key and bar hover/snapshot gating already exist. Hover sweep on the VM: bar 10-14 ms in idle and in the sweep, compositor +1.0-1.3% of a core for 130 motion events/s, flat profile. Nothing to remove.
- **Rejected: `-march=native` and no-LTO builds** (row 3). Scroll CPU of the compositor, 6 interleaved runs, VM i5-8500: LTO generic median 102 ms, `-march=native` 106, no LTO 106 (spread 99-116). Celeron not tested.
- **Rejected: read-coalescing delay** (row 5), **`mmap` of owned files** (row 8: read 0.26 ms vs decode 58 ms for a 465 KB PNG), **per-client whitelist** (row 9: no such path).
- **Kept: IPC `recv` buffers 256/512 -> 4096 bytes** (row 10). 1 MB: 3.26 ms vs 0.29 ms (best of 9). Matters only for floods.
- **Rejected: idle wake-up changes** (section 11 of `PERFORMANCE_FINDINGS.md`). Idle per-process wake-ups/s on the VM: compositor 0.90, bar 1.18, wallpaper 0.00, launcher 0.00 (no file-stat polling found); `show_seconds=true` raises compositor + bar to 7.2 + 3.8 wake-ups/s (+14.6 ms CPU per 40 s, feature cost, off by default); wpctl fallback vs native PipeWire 0-20 vs 10-20 ms per 60 s (noise, 8 runs); lock applet reconnect retry only while the compositor is gone.
