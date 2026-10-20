# Optimizations

What was measured, what changed, what was rejected, and the traps hit along
the way. Numbers come from the `fleetwm-dev` VM (Debian 13, 2 cores, 1280x800
virtio display) unless stated; "idle ticks" are CPU ticks (100 = 1 s) of a
standalone process over the stated window.

Findings from the Lestrix speed round (method, hardware limits, governor, kernel pty and pipe tests, closed doors, and the checklist for the next Fleetwm round) are merged in `PERFORMANCE_FINDINGS.md`.

## Paper-derived practices relevant to Fleetwm (2026-10-11)

The local `developer-performance-opt` corpus is broad: it includes networking,
RDMA, FPGA, JIT, compiler, memory, and measurement papers. The networking,
FPGA, JIT-specific, SIMD parsing, and large-array search techniques do not map
to an identified Fleetwm rendering hot path. The useful transfers are mostly
experimental method and conditional CPU profiling, not new rendering tricks:

- **Treat setup and run variation as part of the result.** Mytkowicz et al.
  (`~/.claude/skills/developer-performance-opt/sources/papers/47_mytkowicz_measurement_bias_asplos09.txt`)
  demonstrate that environment and code/data layout can change conclusions;
  Georges et al.
  (`~/.claude/skills/developer-performance-opt/sources/papers/49_georges_statistically_rigorous_java.txt`)
  caution against selecting best-of-N runs. For a new renderer A/B, alternate
  variants, repeat paired
  runs, record power/frequency and outer-desktop activity, and report the
  distribution (median and spread, preferably an interval on paired deltas),
  not just the best run. This is especially important because the historical
  ~45% Render/3D drag measurement included the live outer desktop and predates
  the drag cap.
- **A hot profile entry is a lead, not proof of a useful fix.** Coz
  (`~/.claude/skills/developer-performance-opt/sources/papers/44_coz_causal_profiling.txt`)
  shows that speeding a sampled function
  may not improve overall progress when it is off the critical path or
  contending with it. Fleetwm's compositor event loop is largely single-threaded
  and the reported drag concern includes asynchronous GPU work, so Coz itself
  is not a direct GPU/present-latency tool here. Keep separating compositor
  CPU, GPU engine busy, frame cadence and input-to-present latency; only
  optimize a CPU path if an ablation changes a user-visible metric.
- **Use profiles to look for duplicated work before instruction tricks.**
  LOADSPY (`~/.claude/skills/developer-performance-opt/sources/papers/03_redundant_loads_loadspy.txt`)
  frames repeated loads as a
  symptom of repeated calls, poor lookup structures or redundant computation,
  not as permission to remove loads without alias/lifetime proof. Fleetwm's
  previous GLES2 scroll profile already identified the large CPU copy as
  Mesa's shm texture upload, not Fleetwm's scene loop. If a new profile instead
  shows Fleetwm code, inspect repeated scene/layout queries and allocations
  there; do not add a speculative whole-program redundancy tool.
- **PGO/BOLT are conditional build experiments.** AutoFDO and BOLT
  (`~/.claude/skills/developer-performance-opt/sources/papers/41_autofdo_cgo16.txt`,
  `~/.claude/skills/developer-performance-opt/sources/papers/35_bolt_cgo19.txt`)
  show the value of representative branch profiles and hot/cold code layout
  in large CPU-bound
  binaries; their data-center results are not Fleetwm expectations. The install
  already uses LTO and PGO, and the training script includes window move and
  resize. Consider a separate profile/build A/B only if a representative
  post-cap CPU profile shows Fleetwm is instruction-front-end/branch bound.
  Do not layer BOLT or change training weights while the GPU bottleneck and
  attribution remain unknown.
- **Do not replace caches or allocators by analogy.** Drepper and Carruth
  (`~/.claude/skills/developer-performance-opt/sources/papers/28_drepper_memory.txt`,
  `~/.claude/skills/developer-performance-opt/sources/papers/53_carruth_cppcon14_algorithms_data_structures.txt`)
  support measuring working-set locality; the jemalloc paper
  (`~/.claude/skills/developer-performance-opt/sources/papers/50_jemalloc_evans_bsdcan06.txt`)
  targets allocator contention and fragmentation trade-offs. Fleetwm already
  byte-bounds finished glass and
  titlebar caches, and its titlebar cache diagnostics report entries/bytes,
  not hit rates. Only add low-overhead hit/eviction accounting if measured
  memory pressure or cache churn makes the trade-off relevant; do not swap
  allocators without allocation-profile evidence.
- **No bit/SIMD hack is presently indicated.** The N4020 lacks AVX/AVX2, the
  existing Celeron `-march=native` scroll A/B was inside noise, and the
  recorded GPU path is not a Fleetwm arithmetic kernel. The relevant
  instruction and bit-trick sources
  (`~/.claude/skills/developer-performance-opt/sources/papers/24_agner_optimizing_cpp.txt`
  through `~/.claude/skills/developer-performance-opt/sources/papers/27_agner_instruction_tables.txt`,
  plus `~/.claude/skills/developer-performance-opt/sources/papers/31_mula_popcount_avx2.txt`)
  advise matching code generation to a profiled loop and target CPU; no such
  loop has been identified in the renderer.

Next evidence-gated experiment remains a controlled post-24-FPS-cap drag
benchmark on the target, with a quiet outer-desktop control, repeated paired
variants, compositor task-clock, Render/3D busy, presentation cadence, and
input latency if measurable. No Celeron benchmark or new optimization was run
for this paper review; laptop activity remains read-only pending explicit
authorization.

## Renderer follow-up: compositor scheduling versus glass drawing (2026-10-11)

Reviewed Fleetwm's current rendering paths and external compositor research
before changing code. No renderer code change is justified by the measurements
available here:

- Fleetwm already caches finished glass tiles and titlebar/frame pictures;
  moving a window repositions scene nodes rather than repainting the client or
  rebuilding its titlebar. The frame cache is shared by flat and glass styles;
  glass adds only the cached, small titlebar/frame treatment while the window
  body remains opaque.
- An existing interleaved Celeron test exercised repeated real titlebar and
  resize-edge drags with glass enabled and disabled. Compositor+bar CPU task
  clock was 591/611/619 ms with glass versus 605/601/621 ms without; Intel
  Render/3D busy was 44.6/45.1/44.8% versus 45.1/45.2/45.1%. These runs show
  no consistent measurable glass penalty or glass-off gain in that workload.
  This active-drag evidence was overlooked in the previous handoff; the
  separate 2026-10-11 idle/unknown-activity sample is still inconclusive.
  Important scope: the drag A/B predates the 24 FPS cap, added on 2026-10-10.
  Its `intel_gpu_top` invocation sampled the whole UHD 600, while Fleetwm ran
  nested inside the live desktop; the absolute ~45% cannot be attributed to
  Fleetwm alone. A verified post-cap, per-process A/B is now recorded in
  `docs/PERF_LOG_2026-10-11.md`; it found a small glass-on Render/3D increase
  but no meaningful CPU or drag-time change, not a glass cost near the reported
  ~40%.
  The titlebar microbenchmark also reduced an 800 px hover redraw from 342 us
  to 51 us with the finished-piece caches. There is no evidence for removing
  Aero glass or adding another glass-specific cache as a response to the
  reported utilization.
- Upstream scene-graph documentation describes damage-aware buffers,
  visibility calculation and direct scanout. Fleetwm already uses wlroots
  scene output/damage, advertises dma-buf feedback, and caps interactive
  move/resize composition at 24 FPS. The remaining unknown is whether the
  user's reported load comes from a different foreground workload or the
  1920x1080 full-desktop setup; the verified nested test is 1280x720.

Research links:

- Xaver Hugl, [Fixing KWin's performance on old hardware](https://zamundaaa.github.io/wayland/2024/06/25/fixing-kwin-perf-on-old-hardware.html),
  2024-06-25. KWin measures CPU and GPU render time and adapts scheduling
  against the presentation deadline using render-time volatility. Its dynamic
  triple buffering overlaps CPU/GPU work when frames miss refresh intervals,
  but may increase latency, waste energy and oscillate around the threshold.
  This is a frame-pacing/throughput technique, not a general reduction in
  work per frame.
- [Wayland Architecture](https://wayland.freedesktop.org/docs/book/Architecture.html)
  describes damage-driven composition and the compositor's role in collecting
  client damage.
- The [wlroots `wlr_scene` API](https://wlroots.pages.freedesktop.org/wlroots/wlr/types/wlr_scene.h.html)
  documents scene-buffer damage regions, visibility and direct scanout.
- The [wlroots renderer API](https://wlroots.pages.freedesktop.org/wlroots/wlr/render/wlr_renderer.h.html)
  exposes render timers whose result may be unavailable. This could support
  profiling, but is not currently wired into Fleetwm's overlay.

Decision: do not cargo-cult triple buffering, driver environment flags, or
additional repaint throttles. Fleetwm already caps interactive composition at
24 FPS, and lowering that cap or changing queue depth trades smoothness/input
latency for load. The prior local `mesa_glthread` experiment reduced CPU about
5% only in a nested test and was deliberately left off on the two-core Celeron.
The current local `build` is `debugoptimized`/O2 without PGO or LTO, so it is
not a valid production renderer benchmark. The verified post-cap Celeron
active-drag A/B is recorded below; it does not support a material glass or CPU
optimization. A frame-time/damage-area trace and a matching 1920x1080 workload
would be needed to target a further change.

### Interactive scene-update trace (2026-10-11)

Source inspection narrows the next measurement without proving a bottleneck:
`Server::update_grab()` in `src/compositor/server.cpp` moves the scene node for
each move event, then updates X11 position and snap-preview state. During
resize it moves the node and sends a size request only when the requested
dimensions change. `output_frame()` in `src/compositor/output.cpp` applies the
24-FPS interactive cap, withholds a not-yet-due commit only when pending damage
is empty, and sends `frame_done` after commit. Thus input-side scene mutation,
client resize work, and output composition are separate possible costs; the
cap alone does not demonstrate that they are expensive or that coalescing
updates is safe. The project targets wlroots 0.18; validate any instrumentation
against the actual built version. Record mutation/configure counts, damage
area, commit/render time, presentation cadence, and input-to-present latency
for move and resize separately before proposing changes.

### DWM, shader cache, and CPU instruction-set findings

- Microsoft's [DWM overview](https://learn.microsoft.com/en-us/windows/win32/dwm/dwm-overview)
  describes application windows being redirected to off-screen video-memory
  surfaces and composed into the desktop; it explicitly lists glass frames as
  a DWM feature. This is a retained-surface composition model, not evidence that
  Aero glass used no GPU or that composition was free. Fleetwm already moves
  retained scene nodes and caches its finished decorations rather than
  repainting them on each pointer event.
- Mesa's [environment-variable reference](https://docs.mesa3d.org/envvars.html#mesa-shader-cache-max-size)
  describes an on-disk cache of compiled shader programs, normally under
  `~/.cache/mesa_shader_cache` (or `$XDG_CACHE_HOME`), with a default maximum
  of 1 GiB. This is a disk-usage ceiling, not a 1 GiB RAM allocation.
  Fleetwm has no custom GLSL shader pipeline or duplicate application-level
  shader cache; its GLES2 programs are owned by wlroots/Mesa. Reusing Mesa's
  cache can avoid shader compilation work during startup, but cannot reduce
  per-frame fill, blending, or GPU busy while dragging. No Mesa cache variables
  were changed.
- The Celeron N4020 has no AVX/AVX2 support. The existing Celeron compiler-flag
  A/B found no scroll-CPU improvement from `-march=native` over generic LTO
  (medians 125.0 ms vs 121.5 ms; runs overlapped), and this was a pixman
  scrolling workload, not a GPU drag test. There is no measured case for
  hand-written SIMD or a new ISA-specific path. `-march=native` in the local
  install build is a machine-local build choice, not a portable binary target;
  it must not be used for binaries intended to run on arbitrary x86-64 CPUs.
- This earlier research note preceded the owner's authorization for an
  isolated nested target benchmark. The verified post-cap A/B is recorded in
  `docs/PERF_LOG_2026-10-11.md`; it provides CPU task-clock and per-process
  Render/3D busy, but not frame cadence or input-to-present latency.

### Task Manager GPU metric accuracy

The Task Manager uses the shared `GpuMonitor`, sampling machine stats once per
second while its window is visible. On Intel, `GpuMonitor` computes
`100 - delta(rc6_residency_ms) / elapsed_wall_time`; this measures the share of
the GT interval not spent in RC6 sleep. An awake-but-idle GT counts as active,
so this is **not render-engine utilization** and cannot quantify rendering
load or headroom. AMD's `gpu_busy_percent` and NVIDIA's `nvidia-smi
utilization.gpu` paths remain enabled; NVIDIA polling stays asynchronous at its
existing reduced cadence. Their definitions and sampling windows differ. The
Intel label is selected from discovered sources rather than replacing or
filtering other adapters. The Task Manager Performance pane now labels a
single-source Intel graph “GT awake (RC6 proxy)” (and uses “GPU activity” for
mixed sources), with an explanatory note; the sidebar calls an Intel-only
series “GT awake”. NVIDIA availability is now exposed before its first
`nvidia-smi` response, preventing a hybrid Intel/NVIDIA machine from briefly
being mislabeled Intel-only. For a Celeron render measurement, use the render-engine
busy field from `intel_gpu_top`; do not substitute the Task Manager figure or
RC6-derived values.

This corrects the interpretation in the older Celeron GLES2 benchmark notes:
their CPU timings remain useful, but the recorded 21–23% Intel RC6-derived
“GPU busy” values are only a GT-awake proxy, not engine busy.

### UI process sampling follow-up (2026-10-11)

An isolated nested Celeron profile separated Settings and Task Manager client
CPU from the compositor and bar. In 10-second idle-visible samples, Settings
used less than 1 ms of task-clock; Task Manager used 163 ms on Processes and
58 ms on Performance, versus 79/48 ms for the nested compositor and 203/195 ms
for the bar. This is aggregate client CPU, not frame latency. The Processes
tab scans `/proc` once per second. A first candidate cached each
`/proc/PID/stat` descriptor's UID rather than repeating `fstat()`; tracing
showed 1,653 calls before versus 213 after per ten seconds, but a balanced CPU
sample did not show a repeatable win. Since a process can change credentials
without changing PID, caching for the descriptor lifetime could make the
displayed User stale; that candidate was rejected and per-refresh UID reads
remain.

The implemented optimization removes a redundant deep copy of the
`ProcessTable` row vector and process-name strings: Task Manager now sorts and
draws the table's own row storage. Three balanced old/new ten-second samples
found client task-clock of 150.70/164.21/168.03 ms before and
142.25/157.90/166.50 ms after. Each paired sample was lower after the change;
the mean fell from 160.98 to 155.55 ms (~3.4%). This is a small CPU-work win
for an idle-visible Processes tab, not evidence of improved input latency,
frame time, or GPU usage.

This profile used an isolated 1280x720 nested compositor, not the owner's
1920x1080 desktop workload. The wl_shm app clients had no separate
`intel_gpu_top` Render/3D client record. Continue with per-frame compositor
damage/presentation profiling for interaction cost; do not infer input latency
from task-clock or background Render/3D utilization.

### Integrated GPU memory baseline (2026-10-11)

The current target is `dev@192.168.0.182` (the owner corrected the older
`192.168.2.156` address). It is an N4020/UHD 600 (`8086:3185`, `i915`) with
3.4 GiB system RAM and an active 1920x1080 eDP panel. It is UMA: "VRAM" usage
is shared system memory, and this driver does not provide the discrete-style
`mem_info_vram_*` sysfs counters. A six-second idle `intel_gpu_top -J -m`
capture reported 61.57-62.07 MiB compositor-client system memory
(62.07 MiB median), with 0-1.21% Render/3D busy (0.41% median). This is a
driver-tracked idle per-client figure, not peak, dedicated VRAM, or a
complete device/system memory total.

This Celeron is one reference target, not the supported-GPU design point.
Fleetwm must remain suitable for Intel integrated and discrete GPUs, AMD
integrated and discrete GPUs, NVIDIA discrete GPUs, and other supported
backends. Do not make memory policy, accounting, or rendering paths depend on
i915, UMA, or `intel_gpu_top`. For meaningful comparisons, record both
dedicated-memory use (where exposed) and shared/system-memory use, alongside
the backend/device and workload; vendor counters are not interchangeable.
Prefer backend-neutral allocation/surface changes, and gate any adaptive
policy on reliable capabilities and measurements rather than vendor names.

The clearest source-level memory candidate is the live wallpaper surface:
`src/wallpaper/main.cpp` rasterizes opaque XRGB8888 at the full output size.
At 1920x1080 that is 7.91 MiB for the client buffer before alignment or GPU
import overhead. Reducing raster dimensions to 1280x720 or 960x540 would
reduce that backing by roughly 4.39 or 5.93 MiB respectively, but needs
fractional viewport scaling and trades image sharpness for memory. Profile
and compare it as an explicit, backend-neutral low-memory option before
adopting a default. The byte savings describe client backing size, not a
guaranteed reduction in dedicated VRAM on discrete GPUs or in driver-reported
GPU memory on any backend.

Fleetwm already supports wlroots direct scanout for exact, unoccluded
fullscreen dmabuf buffers; verify it with a compatible real client instead
of reimplementing it. Mapped clients on hidden workspaces may retain
textures, but eviction trades memory for re-import/work on workspace return
and needs a measured high-water result. The existing 3 MiB glass tile, 2 MiB
titlebar-glass and 1 MiB caption-strip budgets are CPU-side Cairo raster
caches, not a 6 MiB VRAM reservation. `glass_backdrop_for()` can allocate a
full-output CPU pixel buffer (7.91 MiB at 1080p), but no active call site was
found; changing it alone would not reduce current usage. Detailed caveats and
candidate measurements are in `docs/PERF_LOG_2026-10-11.md`.

The earlier 2026-10-07 drag A/B is different: `glassgpu.sh` sampled
`intel_gpu_top`'s `Render/3D.busy` field directly, so its 44.6–45.2% values are
actual device-wide engine-busy reports during that tested workload. The
sampler did not restrict collection to the nested compositor, and the live
outer desktop was running at the same time. Despite the roughly 45% total
engine-busy level, the glass and flat runs overlap; it does not support glass
as the cause or a guaranteed gain from disabling it.

### Drag-path non-pessimization

Window movement repositions the existing scene tree; titlebar and frame
buffers are cached and do not repaint for each pointer event. Removed one
redundant `update_glass_backdrop()` call from the move-grab hot path: Fleetwm's
own window backdrop node is no longer created, so the function only checks a
null pointer and cannot affect the moved window. This trims a needless
cross-translation-unit call per move event without changing scene output.
Expected benefit is small CPU-side event-path work, not reduced GPU busy or a
material glass-rendering speedup. The existing Celeron A/B provides no reason
to sacrifice the glass appearance; further caps, alpha simplification, or
frame scheduling changes need measurements that show a bottleneck.

### Celeron sample attempt (2026-10-11)

A bounded 35-second read-only sample was collected from the live Celeron
compositor (PID 768, glass enabled), aggregating CPU time across all compositor
threads. The seven five-second samples measured 0.2–0.6% of one core (mean
0.46%, median 0.40%). Whether a window was actively dragged during this
interval was not confirmed, so these CPU numbers are not an active-drag
baseline and cannot validate or explain the reported ~40% usage.

The `intel_gpu_top` JSON stream was truncated/malformed before parsing
(82,295 bytes; parser stopped at line 5077), leaving no usable render-engine
busy samples. This attempt therefore provides no GPU result and does not
support a renderer change. Repeat only with a confirmed drag interval and a
bounded/streaming GPU sampler that can reliably return complete records.

### Verified post-cap drag A/B (2026-10-11)

Three balanced pairs were run on the Celeron after the 24 FPS drag cap, using
the installed GLES2 compositor in an isolated 1280x720 nested Wayland session.
Each run confirmed `Fleetwm Settings` in the nested compositor's window list;
the virtual pointer, compositor, and Settings client all used that session's
own sockets. The test repeated titlebar moves and resize-edge drag gestures.
`perf stat` measured compositor-plus-bar task-clock for seven seconds;
`intel_gpu_top -J -s 500` collected 16 half-second samples, and the compositor
PID's client `Render/3D.busy` was analyzed separately from device-wide busy.
The per-trial values and limitations are in `docs/PERF_LOG_2026-10-11.md`.

Across the three pairs, median compositor Render/3D busy over the whole
eight-second capture was 14.45% glass-on versus 13.85% flat; paired glass-minus-
flat differences were +0.54, +1.18, and +0.21 percentage points. Median
compositor-plus-bar task-clock was 501.10 ms versus 497.80 ms, with paired
differences of -8.73, +5.50, and +3.30 ms. The active-sample GPU averages were
less stable because the 500 ms sampling windows did not align exactly with the
2.75-second scripted drag sequence. This is a small glass-associated GPU
difference, not evidence that glass accounts for a reported ~40% load; CPU and
script duration did not show a repeatable glass penalty. Device-wide busy
included the live desktop and peaked near 62%, so it must not be attributed to
the nested compositor. The nested process itself peaked around 43.7-46.4% in
individual 500 ms samples.

No renderer code changed as a result: the measured glass delta is too small to
justify simplifying the appearance, and the CPU numbers do not identify a
Fleetwm CPU hot path. The workload did not reproduce the full 1920x1080 desktop
scenario and did not measure input-to-present latency or GPU time per frame.
Two preliminary batches were discarded after finding a bad Wayland/IPC socket
alias and, in a later attempt, that the pointer helper could inherit the host
display; none of those samples are included. The final trials explicitly
target the nested display. During the discarded host-display batch the helper
could have sent virtual-pointer drag gestures to the live session; it issued
no click command, no settings were changed, and the live compositor was not
restarted. All recorded nested test processes and temporary directories were
removed afterward; live Fleetwm PID 768 remained running.

Source inspection shows one unmeasured candidate for a future investigation:
`Server::update_grab()` calls `wlr_scene_node_set_position()` on each pointer
event, whereas `output_frame()` limits composition to 24 FPS. Instrument
per-frame damage area and position updates before trying to coalesce geometry
updates; the current A/B alone does not establish that accumulated drag damage
is an avoidable bottleneck.

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
Glass versus flat on the compositor at idle: no measurable difference (0.20% against 0.25% on the laptop). The separate active-drag A/B is recorded above.

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
is no glass-only work left to remove. The GPU number is device-wide and includes the live outer desktop; this run
predates the 24 FPS drag cap. It therefore cannot establish Fleetwm's absolute GPU contribution or the cap's effect.
A further "faster glass" change was not made because there is no measured glass-only gap to close.

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

## Round 2026-10-13: memory breakdown, old-hardware backport, text path A/B

**Memory on the dev VM (Debian 13, 2 GB, owner's Task Manager screenshot, 445 MB "in use").** PSS per process (`/proc/PID/smaps_rollup`):
Fleetwm itself is about 55 MB (compositor 27, taskmgr 9, desktop 8, bar 7, rest small; 12 MB of the compositor and 3-4 MB per client is shared
window buffers). The rest of the figure is not Fleetwm: `lestrix` 111 MB (of which 36 MB is libLLVM, i.e. Mesa llvmpipe, which only a GPU-less VM
loads), Xwayland 42 MB (24 MB file-backed), tailscaled 77 MB RSS, journald 36 MB RSS, kernel slab 51 MB. Idle CPU of every Fleetwm process is
under 0.2% of a core (30 s sample).

**Xwayland kept alive by an SDL2 client (fixed).** `lestrix --lite` (SDL2) opens two X11 connections even in a Wayland session, so the lazily
started Xwayland never exits. A/B on the VM: default 6 sockets on Xwayland, `SDL_VIDEODRIVER=wayland` or `wayland,x11` 4 (none from the client, Xwayland
not started at all); with the client gone Xwayland exits after its 10 s `-terminate`. The compositor now exports `SDL_VIDEODRIVER=wayland,x11`
(not overriding an existing value). The saving is Xwayland's anonymous and shared memory (~18 MB), not its 42 MB PSS (file pages are cache).

**Text path (`src/fleetkit/fleetkit.cpp`).** Bench `perflog/2026-10-13/textbench.cpp`, i5, median of 7, 2,000 labels: `draw_text` 11.4 ms -> 8.8 ms
(cached font face + one UTF-8 to glyph conversion instead of two); 20,000 `measure_text` calls 22.2 ms -> 0.7 ms (bounded per-thread cache, only with an
identity matrix, keyed by string, size, weight, device scale). Pixels identical (`fleetwm-fm --screenshot` old and new `cmp` equal; unit tests 22/22). End to
end the file manager's start-to-PNG time did not move (82.1 vs 82.5 ms median of 15): the win only exists where many labels are measured per repaint. Inter
is not installed on the bench machine (Noto Sans was measured).

**Backport to older Debian (feasibility, versions from sources.debian.org).** Blocker is wlroots >= 0.17 (0.18 targeted; 0.18 needs wayland >= 1.23,
libdrm >= 2.4.122, meson >= 0.59, pixman >= 0.42, libinput >= 1.19). Stock packages: wlroots buster none, bullseye 0.11, bookworm 0.15, trixie 0.18,
forky 0.20. Wayland: 1.16 / 1.18 / 1.21 (1.23 in bookworm-backports) / 1.23.1. libdrm: 2.4.97 / .104 / .114 (.123 backports) / .124. meson: 0.49 / 0.56 /
1.0.1 (1.7 backports) / 1.7. Result: bookworm (12) is reachable with backports plus a source build of wlroots 0.18; bullseye (11) and buster (10)
would need wayland, libdrm, libinput, meson and wlroots all built from source (or a port to wlroots 0.11/0.15 APIs). The language is not the blocker:
a `-std=c++17` build fails only on 17 `operator==() = default` (C++20), a missing `<unistd.h>` in 4 places and one `std::erase_if` (227 errors, 5 kinds).
Not done: nothing was built on an older distribution; GCC 8 (buster) `std::filesystem` needs `-lstdc++fs`.

### Callgrind of the compositor, and the icon scaling it found (2026-10-13, same round)

Method: the training workload (`scripts/pgo-train-session.sh`, 40 s, headless backend, pixman renderer, no foot/grim/wlrctl installed so those parts skipped)
with the compositor started under `valgrind --tool=callgrind` (instruction counts are deterministic where wall time is noisy). Caveat: this is the software
renderer; on a GPU renderer the pixman share (about half of the 858M instructions) does not exist, so only the ratios inside Fleetwm's own code carry over.

- **Titlebar app icon was scaled with `CAIRO_FILTER_BEST` on every titlebar render** (`draw_titlebar`, `cairo_paint_with_alpha`): 64.3M of the 229M instructions
  in `draw_titlebar` (28%). Now `kit::draw_app_icon` (scaled once per icon and size, then a plain copy): 64.3M -> 0.88M. Per titlebar render 4.49M (51 renders) ->
  2.92M (62 renders) instructions, both including about 60M of one-time font loading; whole compositor 858M -> 792M (-7.7%) on this workload. Not yet measured
  on a GPU renderer or in wall time.
- **`draw_app_icon` keeps the scaled copy on the icon surface** (cairo user data) instead of a process-wide map keyed by the surface pointer. The old map never
  freed a copy and could give a new surface that reused a freed address a stale image, which made it unsafe for the tray (icons are replaced). Now any image
  surface may be passed. Test: `DrawAppIcon.ANewIconDoesNotInheritAFreedOnesCopy`.
- **Same pattern removed in the tray (2 sites in `src/bar/main.cpp`) and in the launcher result rows (2 sites in `apps/launcher/main.cpp`).** Expected saving is
  the same per-paint scaling (about 1.9M instructions per icon per repaint in the titlebar case); not measured separately for these.
- Seen and left alone: prewarm `record_to` builds one `Range` (with a copied path string) per resident page and then sorts them, 87M instructions (10% of this
  run), but it runs once per program per build; `load_desktop_entries` 26M (3%), once at start.
- End-to-end on the dev VM (2 cores, headless training run of 85 s, CPU seconds of the whole session from the cgroup; 3 interleaved pairs), installed-HEAD build
  against the build with only the text-path and SDL changes (no icon fixes yet): old 13.1, 13.5, 13.2 s; new 13.3, 13.7, 13.2 s; session PSS at 60% of the run
  106.9, 99.2, 98.8 MB against 107.5, 101.0, 97.3 MB. No difference outside the noise, as expected (the training run starts no SDL client and measures few labels).
- Re-run with the icon fixes (PGO build of the full working tree against the installed-HEAD PGO build, same 3 interleaved pairs): old 13.6, 13.7, 13.5 s; new
  13.2, 13.6, 13.6 s; PSS 109.1, 107.3, 107.4 MB against 97.3, 107.5, 105.6 MB. Medians 13.6 against 13.6 s: **no end-to-end difference at this resolution** (the
  spread between runs is about 0.4 s, 3%). Consistent with the callgrind figure: about 1.6M instructions saved per titlebar render and roughly 100 renders in
  85 s is 160M instructions, about 0.05 s of 13.6 s. The saving only shows in bursts of titlebar redraws (resize and drag at up to 24 renders a second, focus
  changes with many windows), and this workload does not have enough of them; a wall-time or GPU-busy measurement of a resize on the Celeron laptop is still open.

Reproduce: `perflog/2026-10-13/ab.sh LABEL SRC_DIR [seconds]` (one training session of `SRC_DIR/build-pgo`, CPU from the cgroup, PSS sampled at 60% of the run).

## Round 2026-10-20: Windows 7 glass, drop shadow, caption glow

All numbers: this build machine (Intel i5, `-O2` bench build of fleetkit), `perflog/2026-10-20/titlebar_glow_bench.cpp` (800 px titlebar, glass on, background and caption strip caches warm; 2,000 renders per figure, three runs, the spread was under 3%).

| Titlebar render | Light text (theme tint) | Aero caption, first version | Aero caption, finished picture kept |
|---|---|---|---|
| same title (a hover or focus redraw) | 73 us | 111 us | **73 us** |
| a new title every time | 74 us | 210 us | 205 us (the halo is built once per title) |

- **Finding:** painting the blurred halo through an A8 mask on every redraw (`cairo_mask_surface`, about 190x44 px) cost 38 us, half of a hover redraw. `draw_text_glow` now keeps the finished picture (halo and text, ARGB) per (text, size, weight, colours, quarter-pixel position) in a 512 KB cache and blits it: a hover redraw costs what it did before the glow existed. Building the halo (3 box passes on the glyph coverage) is 130 us once per title, on the Celeron a few hundred us per title change.
- **Frost layer and shadow cost nothing to draw:** the frost is four scene buffers showing parts of one shared blurred picture (a source-box update when the window moves), the shadow is eight scene buffers cut from one 80 px picture per focus state (positions and sizes only). No pixel is drawn when a window moves or resizes. What they add is compositing area (about the frame ring plus the 20 px shadow ring per window). Drag cost with and without each: see the table below (VM, pixman, headless; the GPU side needs the Celeron).
- Test: `TextGlow.HaloAroundDarkTextAndTheKeptPictureMatches`, `WindowShadow.FadesOutwardAndFocusedIsDarker`.

Drag cost, `scripts/grab-stats-run.sh build-test OUT --renderer pixman --size 1280x720 --glass G --rounds 3` with `WINDOW_SHADOWS=S` (dev VM i5-8500 KVM, 2 cores, CPU rendering; `commit_ms_avg` is the compositor's time per frame while a Settings window is dragged / resized; one run per row, so differences under about 10% are noise):

| glass | shadows | move ms/frame | resize ms/frame | damage px/frame (move) |
|---|---|---|---|---|
| off | off | 1.01 | 1.16 | 532k |
| off | on | 1.30 | 1.40 | 565k |
| on (frost) | off | 1.13 | 1.25 | 532k |
| on (frost) | on | 1.21 | 1.36 | 521k |

Reading: with CPU compositing the shadow ring adds about 0.2-0.3 ms per frame (+20-29%, more damaged area) and the frost layer about 0.1 ms (+8-12%); together +17-20%. On a GPU the cost is texture sampling over the same areas and is expected to be small, but that is not measured (the Celeron was off). `window_shadows = false` in theme.toml removes the shadow cost; the default stays on because the shadow is a large part of the Windows 7 look, and the owner decides after the hardware test.

**Rejected: opaque region on the frost layer (2026-10-20).** Idea: `wlr_scene_buffer_set_opaque_region` on the four frost pieces so the scene would not draw the wallpaper below the frame. A/B on the pixman rig, glass on, shadows off, `commit_ms_avg` per frame, 4 interleaved runs each, 3 rounds per run (dev VM, CPU rendering):

| | move ms/frame (4 runs) | mean | resize ms/frame (4 runs) | mean |
|---|---|---|---|---|
| without | 1.27, 1.25, 1.28, 1.29 | 1.27 | 1.35, 1.39, 1.40, 1.37 | 1.38 |
| with | 1.28, 1.29, 1.24, 1.25 | 1.27 | 1.37, 1.41, 1.40, 1.39 | 1.39 |

No difference (the spread inside one build is larger than the gap), so the change was removed. A first attempt at this A/B compared two identical binaries (the VM's `ninja` thought `view.cpp` was up to date because the extracted file had an older timestamp than the object: after syncing sources to the VM, `touch` them and check with `cmp` that the binaries differ before measuring).

## Own TOML reader and writer instead of toml++ (2026-10-20)
The installer's unity build fetched toml++ with `git clone` on a fresh checkout (`subprojects/tomlplusplus.wrap`), and the system package had to match. `src/common/fleettoml.{hpp,cpp}` replaces it: the same `toml::table` / `node_view` / `parse_file` / `insert_or_assign` interface for the part the project uses, tables kept as sorted vectors, one pass over the text, output written straight into a string.

Measured with `perflog/2026-10-20/fleettoml_diff/` (g++ -O3, this build machine, toml++ 3.4.0, 3 runs agree within 5%):

| document | toml++ parse | fleettoml parse | toml++ write | fleettoml write |
|---|---|---|---|---|
| a 28-line `theme.toml` (20,000 runs) | 12.9-13.2 us | 4.3-5.4 us | 2.9 us | 1.5 us |
| 400 folder-view tables (500 runs) | 972 us | 582-594 us | 231-244 us | 105 us |

Checks that it is a safe replacement (`perflog/2026-10-20/fleettoml_diff/run.sh TOMLPP_INCLUDE`): ~200,000 mutated documents (a hand-written list, the repo's real config files, the test snippets, and random single-character deletions, insertions and replacements), parsed by toml++ 3.3.0, toml++ 3.4.0 and fleettoml: zero cases where fleettoml rejects what toml++ accepts, zero where it accepts what toml++ rejects, values equal (dates are kept as text, the project has none); round trip `parse(print(parse(x))) == print(parse(x))` on every parsable document; ASan and UBSan clean on those plus 600,000 random strings; printed output matches toml++'s layout (values first, then `[tables]`, then `[[arrays of tables]]`, literal single-quoted strings; differences: numbers print in decimal, inline tables print as sub-tables, strings with control characters are escaped). Two toml++ behaviours are kept on purpose because the config code relies on them: an integer in a bool field reads as true unless it is 0, and a missing file is a `parse_error`. The whole unit suite (1,187 tests) passes unchanged except for the one comment that named toml++.
