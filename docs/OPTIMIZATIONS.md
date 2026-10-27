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

## Glass backdrop built from the windows below (2026-10-20)
`src/compositor/glass_scene.cpp`: pictures of what lies under a window's glass frame, a quarter of the size, so the frame can show the other windows blurred. Cost control, in order of effect: no window under the glass means no picture (key 0, the shared frost is exact); only the pixels that lie under the glass are read back from a client (a titlebar strip of a 700 px window is about 100 KB, not the 2.8 MB window); pictures are rebuilt at most every 45 ms and only when `glass_backdrop_key` changed (a commit of a window below, a move, a restack); the shared wallpaper and ray pictures stay as before. Measured (`scripts/grab-stats-run.sh`-style run, dev VM, pixman, 1280x720, three terminals, the front one dragged over the others, 12 drags of 40 pointer events): `glass_builds` 5 per drag, `glass_ms` 8.0-8.9 per drag (1.6 ms a rebuild), `commit_ms` 0.94-1.04 average (1.0 before). New GRABSTATS fields `glass_builds` and `glass_ms`. Not measured: GLES2 on the Celeron, where `wlr_texture_read_pixels` waits for the GPU.

## Round 2026-10-22: findings carried over from Lestrix, Painite and CreeperCrest
Sources: Lestrix `0289c8e` (cursor stops blinking when idle, job threads on demand, idle skips, cache statistics), Painite `0e55324`/`79d19b7` (send only changed state, rebuild only when a signature changes, `cache` hit rates), CreeperCrest backlog (no subprocess per poll when nobody looks at the result).
Applied, 22 unit tests and `scripts/smoke-test.sh` (13 checks) pass:
- `src/bar/main.cpp`: `sync_stats_timers()` arms the 2 s stats and 5 s disk timers only while `stats_shown()` (Metrics element visible or a non-taskbar style); `reload_config` re-syncs and refreshes. That also stops the `nvidia-smi` spawn every third tick.
  Measured with a headless compositor (`WLR_BACKENDS=headless`, `theme.toml` window_layout desktop, Metrics hidden by default), bar under `strace -f -e trace=openat,statfs,execve`, 30 s window after 5 s warm-up, old and new binary alternated twice: voluntary context switches 1096 / 1117 (old) against 787 / 782 (new); `/proc/meminfo` opens 19 against 2; `statfs` 7 against 1; CPU 0-2 ticks either way (below resolution). The strace overhead inflates absolute counts; the ratio is the result. Not measured: the `nvidia-smi` spawn (no NVIDIA machine), the Celeron laptop. The tiling layout shows the stats, so it is unchanged there.
- `src/fleetfm/window.cpp`: thumbnail worker starts in `request_thumb`, not in the constructor (not measured; one idle thread per window).
- Cache counters: `kit::text_cache_stats()` (text measure, caption glow) and hits/misses/evictions in `TitlebarCacheStats`; shown in the Task Manager Cache tab for that process's own caches; unit test `TitlebarDraw.CountsHitsAndMisses`. The counting costs one increment per lookup (not measured separately).
Checked, nothing to do: no caret blink or idle animation loop (busy ring and charge sweep exist only while active); `wpctl` polling is only the PipeWire fallback; transfer progress already redraws at 250 ms and file search results arrive in batches of 200 or 150 ms, so the Lestrix flood cap has no equivalent to add; icon cache already counted hits.
Declined: slowing the bar's refresh when idle (Lestrix idle bar every 3 s). The bar has no idle signal and its numbers are meant to be glanced at live.

### Round 2026-10-22, PGO re-measured against no PGO, and a more aggressive PGO tried
Three builds of the same tree on the dev PC (i5-8500, 2 CPUs, 7 GB), all with the install flags (-O3, `-march=native`, LTO, unity): v0 `scripts/build-pgo.sh none`; v1 `build-pgo-auto.sh` (85 s training, `-fprofile-partial-training`); v2 `FLEETWM_PGO_AGGRESSIVE=1 build-pgo-auto.sh` (`-fprofile-update=prefer-atomic` while counting, `-flto-partition=one` for the final link).
Build cost: v0 279 s, v1 701 s, v2 629 s (training included); largest single process 415, 445 and 548 MB.
Run cost: `scripts/pgo-train-session.sh` (85 s, three rounds, headless, pixman) against each build, three runs each, alternating, CPU ticks (100 = 1 s) of each program with `FLEETWM_PGO_CPU_REPORT`:
- compositor, 6 runs per variant (two passes): v0 143 143 133 137 135 142 (median 138.5); v1 137 139 111 115 138 118 (128); v2 133 133 132 111 141 140 (133).
- second pass, all programs summed: v0 224, v1 197, v2 236 (medians of 3). The PGO runs split into a low group (111-118) and a high group (138-141) with the same binary, so run-to-run noise is as large as the difference.
Result: PGO is at best 4-8% fewer ticks in the median and not outside the noise; the aggressive variant is no better than the plain one and needs more link memory. Neither is a measured win; the existing PGO step stays as it is (it was already measured as "no gain" on the laptop in `PERFORMANCE_FINDINGS.md`), and the aggressive flags stay opt-in. Training longer would not add coverage (85 s already runs all six look rounds), so no longer training was built.
Not done: a deterministic comparison (callgrind instruction counts of a fixed scripted scene) which can resolve a few percent; BOLT (earlier BOLT 19 trial: no gain, 8x larger binary); PGO of wlroots and Mesa (system libraries, where most of the compositor's time is spent).
### Code scan results (read-only, not profiled)
Applied: desktop entry parsing above (it ran once per launcher start, per start menu, per first icon lookup in every program). Candidates left, by file: compositor `glass_scene.cpp:130-146` `glass_views_below` (sorts and builds vectors per call, O(n^2 log n) per geometry update across n windows) and `view.cpp:477` `glass_backdrop_key` on every geometry update; bar `popup_running` (`/proc/*/comm` scan once a second while autohide waits); bar tooltips re-reading `/proc/meminfo` and per-core `scaling_cur_freq` every second while open; compositor `already_running` forks `pgrep -f` for each helper at each output start; `fleetfm/window_draw.cpp:85,469` and `icons.cpp:31` create a cairo gradient pattern per box per draw; `fleetkit.cpp` glow key built with `snprintf` on every call and `layout_with_fallback` allocating vectors per call; blur/tint passes allocating a scratch vector per rebuild (`glass_scene.cpp`, `glass_tint.cpp`, `window_shadow.cpp`, `backdrop.cpp`); toml files re-parsed without an mtime guard (`power_config`, `keybinds_config`, `default_apps`, `output_config`, `fm_settings`, `desktop`; call frequency not checked). None was profiled, so none was changed. Checked and dropped: skipping the window-list broadcast without subscribers (the bar always subscribes).

### Scan candidates measured, none changed (2026-10-22)
Each candidate from the code scan was timed on the dev PC (i5-8500) before touching it:
- `FmWindow::box` / `fill_v` gradient pattern per draw: creating, filling with two stops and destroying a cairo linear pattern costs 0.057 us; a 300x24 gradient box costs 4.9 us (solid: 1.2 us), 300x60 9.9 us (solid 2.3 us). The cost is rasterising the gradient, not the pattern object, so caching the pattern gains nothing; caching a pre-rendered strip would save about 3 us a box, 0.15 ms for a 50-box list, and only when the window redraws at all. Rejected.
- Bar `popup_running()`: one scan of `/proc/*/comm` (212 processes) costs 1.83 ms, run once per second only while autohide waits and a popup is open. Rejected.
- Compositor `glass_views_below` sort: a sort of about 20 pointers per call, called O(windows) times per window change, against a measured 1.6 ms per glass rebuild (earlier section, rebuilds already limited to one per 45 ms and key-gated). Not worth a change; the picture rebuild is the cost and it is already gated.
- `pgrep -f` forks at output start: once per helper per output at start and hotplug. Not hot.
- Config files parsed in `power_config`, `keybinds_config`, `default_apps`, `output_config`, `fm_settings`, `desktop`: every call site runs at start or on an inotify reload, or when a surface is made (`apps/desktop/main.cpp:201,788`). Not hot.
- Bar tooltip re-reads (`/proc/meminfo`, per-core `scaling_cur_freq`): once per second only while the tooltip is open.
Graphics calls, measured with `scripts/grab-stats-run.sh build perflog/2026-10-22/grab --renderer pixman --size 1280x720 --rounds 2` (raw files in `perflog/2026-10-22/grab/`): the compositor used 0 CPU ticks in 8 s of idle and committed 2-3 frames in 13-17 s of idle (damage 28,000-400,000 px, the clock and the first draw); window move 8.9 commits/s with the drag cap waiting 45 times a second; resize 21 commits/s, damage about two thirds of a 1280x720 screen per commit (the window itself); `glass_builds` 0 in all phases (no window under a glass frame in this scene). Nothing there is a repeated draw that could be dropped.

### Glass backdrop kept at its own size (2026-10-22)
`glass_backdrop_for(out_w, out_h)` used to paint the blurred wallpaper (480 px wide, already blurred) out to a buffer the size of the output, one per output size and for the life of the session: 3.7 MB at 1280x720, 8.3 MB at 1920x1080, 33 MB at 4K, and the same again as a texture on a GLES renderer. Now `glass_backdrop()` returns the small picture and `View::update_glass_backdrop` aims the scene's source box at the part under each frame piece (`kit::backdrop_map` / `kit::backdrop_source` in `fleetkit/backdrop.hpp`; `paint_backdrop` uses the same cover-fit map), and the scene scales it with its bilinear filter. The source box is kept half a pixel inside the picture.
Found by listing the largest mappings of the compositor's `/proc/PID/smaps` in a headless session with glass on and off (`scratchpad` script `mem.sh`: Settings, Task Manager and file manager open, 15 s): glass on had one extra 3884 kB anonymous mapping.
Measured, compositor, pixman renderer, 1280x720: anonymous 8028 kB -> 4416 kB with glass on; glass off 3160 kB (3044 before, noise). PSS 27.0 -> 23.3 MB. The other programs are unchanged (bar 4.6 MB PSS, desktop 5.1, file manager 7.7, Settings 5.7, Task Manager 6.0, wallpaper 0.7, lock applet 1.1). Not measured: a real GLES session (the texture side, expected to shrink by the same amount), the Celeron laptop, a screenshot of the finished frame (no screenshot tool installed here; the pixel check in `GlassBackdropMap` is a cairo model of the renderer's bilinear sampling, not the renderer itself).
What else sits in the compositor's 15.3 MB of shared memory (same session): the output's swapchain buffer 3.6 MB (a /dev/shm buffer under pixman; GPU memory on a real card), the wallpaper picture 3.6 MB (full resolution, needed), client surface buffers about 7.5 MB (Settings, Task Manager, file manager and desktop, two buffers each). None of those is waste at this size.

### Glass rebuild with windows under the frame (2026-10-22)
Scene: `scripts/grab-stats-run.sh build OUT --renderer pixman --size 1280x720 --rounds 1 --terminals 3` (three `foot` terminals printing `seq 1 60` every 80 ms, the last one dragged by its titlebar and resized by its right edge; headless, pixman, glass on). Compositor CPU in 8 s phases (ticks, 100 = 1 s) and GRABSTATS per second (raw: `perflog/2026-10-22/glass/`):
| | idle | move | resize |
|---|---|---|---|
| before | 41 ticks, 18.3 builds/s, 26.3 glass_ms/s | 67, 23.3, 42.3 | 71, 23.2, 40.1 |
| blur routine + 120 ms for content-only changes | 23, 9.4, 11.0 | 62, 25.6, 28.6 | 63, 19.8, 24.5 |
| glass key without the glass position | 23-24, 10.3, 12.7 | 50, 12.3, 15.0 | 54-55, 12.9, 15.0 |
| glass off (same scene) | 7 | 24 | 32 |
Found with callgrind on the compositor (`valgrind --tool=callgrind`, 60 s idle, same scene, 3 scrolling terminals): `glass_build_pictures` was 68% of all instructions, and inside it the box blur (`gs_box_blur`: an integer division per sample, column walks with a stride, a clamp per sample) was most of it. After the routine was shared and rewritten (`src/fleetkit/box_blur.cpp`: one running sum per column walking down rows, four channels together, division by multiplication checked at set-up) the blur still is 61% of a rebuild at 6.6M instructions in a generic x86-64 build (the install builds with `-march=native` and vectorises the vertical pass); micro-benchmark, 3 passes of radius 3 on RGBA, old against new: 181x24 245 -> 78 us, 20x95 106 -> 41 us, 480x270 8.8 -> 2.2 ms.
What each change does: (1) `kit::box_blur_u8` replaces `gs_box_blur`, `box_blur_a8` (caption glow), the window shadow's `box_blur` and `box_blur_rgba` (wallpaper); `tests/test_box_blur.cpp` compares it with a copy of the old loop on 180 combinations of size, radius, passes, channels and padded stride. (2) `Server::mark_glass_dirty(view, delay)`: a layout change (a window below moved, resized, appeared, focus changed) is rebuilt after 45 ms as before, a content-only change after 120 ms (`kGlassContentDelayMs`); the timer is no longer pushed back by every request (it only moves earlier). (3) `glass_backdrop_key` mixed in the glass's own position, so every step of a drag changed the key and rebuilt; the pictures are laid out in layout coordinates and hold a 32 px margin, so they stay exact while the glass moves inside that area. `View::update_glass_backdrop` now rebuilds when the key changed or a frame part is no longer inside the area its picture covers.
Not measured: GLES (the texture upload and the readback of client buffers are what GLES adds), the Celeron laptop, a screenshot of the frame while dragging (no screenshot tool here): the pictures can lag a moved window below by up to 120 ms, which is blurred under the glass. What is still glass-specific in the 23-24 idle ticks against 7 without glass: about 10 are rebuilds (a rebuild is about 1.1 ms: blur, reading the client's pixels, a new buffer for each of four parts) and the rest is composing translucent frames (output commit 0.33 ms against 0.17 ms). Next ideas, not done: skip replacing a picture when the new one is byte-identical (no damage, no commit), rebuild only the part of a frame piece that overlaps the changed window, a smaller margin than 32 px.

### Glass second pass: stage timings, the drag path, per-part rebuilds (2026-10-22)
Same scene as above (`scripts/grab-stats-run.sh build OUT --renderer pixman --size 1280x720 --rounds 1 --terminals 3`, compositor CPU ticks per 8 s phase; raw files `perflog/2026-10-22/glass2/`).
| step | idle | move | resize |
|---|---|---|---|
| start of the day (glass on) | 41 | 67 | 71 |
| after the first pass (blur routine, 120 ms content delay, key without the glass position) | 23 | 50 | 54 |
| + per-part keys, identical pictures kept, vectorisable blur | 20 | 49 | 54 |
| + cheaper shrinking of the windows below | 16 | 42 | 52 |
| + grab follows the pointer once per frame | 15-17 | 31 | 44-48 |
| glass off, same scene (with the grab change) | 8 | 19 | 30 |
Stage timing inside a rebuild (new GRABSTATS fields `gl_wall_ms`, `gl_content_ms`, `gl_blur_ms`, `gl_post_ms`, idle, ms per second): before the shrink change 0.5 / 4.7 / 3.7 / 0.1, after 0.4 / 1.5 / 3.6 / 0.1. The blur of the glass pictures is now the largest part of a rebuild (63%); in a generic build (`-O3`, no `-march`) a 181x24 RGBA blur is 71 us, with `-march=native` 37 us (the old loop 250 us), so an install gains more than this dev build shows.
The drag path: callgrind of the move phase showed `View::update_glass_backdrop` at 37.9% of all instructions, and 36.8% of everything inside `wlr_scene_buffer_set_source_box` (193,296 calls, about 20,000 instructions each; the scene recomputes damage and visibility over the tree for each change). The dragged window's frost and ray pieces (eight scene buffers) change on every pointer event. wlroots already returns early for an unchanged box (caching the last box in the view changed nothing: 42 to 43 ticks), so the fix is to ask less often: `Server::note_grab_motion` records the motion and asks for a frame; `Server::flush_grab_motion` applies the grab (position, glass, snap preview, resize request) in `output_frame` just before the commit, and in `end_grab`, so the window ends where the pointer ended. `GRABSTATS`: events 125/s, positions applied 16-20/s. The frame-rate cap's timer path treats a pending grab as pending work, so a drag cannot stall behind the cap.
`glass_same`/`glass_pieces` in this scene is 40/41 per second at idle: the pictures come out identical to the ones they replace because the terminals repeat the same final screen; with a log that scrolls continuously the share will be lower. Not measured: GLES, the Celeron laptop, a screenshot of a drag (no screenshot tool here). Behaviour change to know about: while dragging, the window is placed with the pointer position at the time the frame is committed, not at the time of each event; with the 24 FPS drag cap the picture on screen was the last position of the same frame before, so what is shown is the same except that it is one sample newer.

## Round 2026-10-26: remote desktop server cost (fleetwm-remote)

Workload: dev VM (Debian 13, headless compositor, pixman, 1280x720), one terminal printing `seq 1 60` every 80 ms (about 12 screen updates a second), one VNC viewer pulling incremental updates for 18 s (`rfb_load.py`-style loop, CPU read from `/proc/<pid>/stat` over 12 s). Compositor CPU: 0.6% with no viewer, 1.1% with one (the grab with damage is cheap). Server CPU per viewer: Raw 18.7% of a core at 41 MB/s, ZRLE 6.5% at 314 KB/s (one run). Then the ZRLE tile encoder was changed: solid and palette tiles are found in one pass over the raw pixels (a new colour is looked up only when it differs from the previous pixel's), only the palette entries are converted to the viewer's pixel format, no heap allocation per tile, and the tile buffers handed out by the hub are reused between updates. Result, same load, same bytes on the wire (314-321 KB/s): ZRLE 4.2, 4.2 and 4.4% of a core (three runs), about 35% less. The decoded picture is unchanged (the test client checks every tile and the length of the deflate stream). Not measured: RDP CPU (uncompressed bitmaps, no encoder work to tune), a real network, more than one viewer, a 4K output.

### Remote desktop, second pass (2026-10-26): CopyRect, unchanged tiles, RLE, limits

Same VM and method as above. Workload B: one terminal 1000x600 in the 1280x720 output printing a random 120-character line every 100 ms (so every line scrolls the whole window). VNC, ZRLE, one viewer that only counts bytes, 16 s: without CopyRect 1734 KB/s and the daemon 142 permille of a core; with CopyRect 157 KB/s and 53 permille (bytes -91%, CPU -63%); 149 of 155 updates were CopyRects, and a slower scripted viewer that applied 71 CopyRects ended with 0 differing pixels below the bar against a fresh Raw picture. Each scroll update took 220 tiles from the hub and sent 12. Detection cost is in the numbers above (row hashes of the changed block, one memcmp pass to verify). RDP, FreeRDP 3.15, 14 s on workload B at 0.2 s per line: 32 bit raw 154.2 MB (76 updates), 16 bit run-length coded at quality 50 35.9 MB (72 updates), TLS off 154.2 MB (the same bytes, no cipher cost measured). Limits (VNC, no CopyRect, quality 90): no limit 1717 KB/s, 256 KB/s limit 264, 64 KB/s limit 76 (an update of about 150 KB is not split). Quality alone on this text workload changed bytes little (quality 50: 1569, quality 30: 1643 KB/s, the viewer's own request rate caps the update rate at about 10/s); the update interval now grows to 100/200/400 ms below quality 50/30/10, and quality matters most for RDP (16 bit) and for photographic content, which was not measured. Idle: 0 voluntary wake-ups in 30 s with no viewer (before: one per second by design, not measured). Not measured: RDP CPU per viewer, a real WAN link (the limit and link learning were checked on loopback), 4K, several viewers.

## Memory budget of the whole desktop (2026-10-26)

Measured on the dev VM (Debian 13, 2 GB, the real session of user dev, my test sessions stopped) with `scripts/ram-budget.sh`, PSS so shared
libraries are split fairly: **Fleetwm 43.5 MB** in 6 processes (compositor 18.4, desktop 8.3, bar 7.5, greeter 6.9 which stays resident
on tty1, lock applet 1.6, wallpaper 0.9). The system as a whole uses 517 MB (total minus available): the biggest non-Fleetwm items are
tailscaled 98.5 MB, journald 46.5 MB, the Samba and Apache servers set up for the file manager tests (about 35 MB), PipeWire/WirePlumber 14 MB,
udisks 6 MB, and 61 MB of kernel memory that cannot be given back. So **the desktop is about a fifth of a 200 MB budget, but the
whole system cannot be below 200 MB on this VM**: removing tailscaled, Samba, Apache and capping journald (`SystemMaxUse=`, `RuntimeMaxUse=`)
would take about 180 MB off and still leave roughly 330 MB of system use. A system under 200 MB in total needs a minimal OS image (no
tailscale, no Samba, a small journal, few services) on top of the 43 MB. Candidates inside Fleetwm, none done: the resident greeter
(6.9 MB), one desktop-icon process per output (8.3 MB), a smaller compositor heap. Each needs a measurement against responsiveness first.

Remote desktop memory: fleetwm-remote idle 3.7 MB PSS; with one VNC and one RDP viewer at 1280x720 it was 26.4 MB, then 21.7 MB after the
tile buffers are given back after a full picture, the encoder buffers are freed when they grew past 256 KB and glibc's thresholds are pinned
(`tune_malloc_for_low_rss`). Each viewer holds its own screen copy (3.7 MB at 720p, 8.3 MB at 1080p) so a viewer costs about 9 MB at 720p.
Bar updates from the daemon were cut from every second to every 10 s and at each change.

Fixed (2026-10-26): the autostart check compared `pgrep -f <full path>` with helpers started by bare name, so it never matched and a killed
compositor left its lock applet (0.7 MB PSS) behind for the next one to duplicate. `already_running()` in `src/compositor/server.cpp` now
compares `/proc/<pid>/exe` with the installed path, which also drops one fork+exec of `pgrep` per helper at start-up. The resident greeter
calls `malloc_trim(0)` once its login compositor is gone (anon heap 4.1 MB of its 7.2 MB PSS; effect not yet measured, needs a logout on a real greeter).

Where the rest sits (dev VM, PSS): compositor 20.4 MB = 7.8 anon + 4.1 wallpaper buffer (one full-screen XRGB, inherent) + 2.3 toolkit shm + libraries;
bar 6.9 (3.1 anon); desktop 6.1 (2.2 anon); greeter 7.2. Lightweight-desktop idle figures from other projects could not be found
(search came back empty), so there is no outside reference to compare against.

### Second memory pass (2026-10-26)

- **libcrypto no longer loaded by programs that never hash.** `src/fleetfm/hasher.cpp` opens `libcrypto.so.3` with `dlopen` on the first checksum (the header is used for types only); before, every program that linked the file manager core (desktop, Settings, file manager, ...) mapped it and paid for its relocations at start-up. A/B on the headless rig, `fleetwm-desktop` with `LD_PRELOAD=libcrypto.so.3` (the old state) against without: **3621 -> 3023 KB PSS (-0.6 MB, -17%)**, anon 1988 -> 1448 KB, one desktop process per screen. `-Wl,--as-needed` is now set project-wide (it removed nothing else; every other linked library is used).
- Allocator: `M_ARENA_MAX=2` for the helpers (a few threads share one extra arena instead of each growing a heap). Unmeasured.
- Greeter: after the login compositor is gone it runs `malloc_trim(0)` and asks jemalloc (preloaded by the service) to purge all arenas. Unmeasured: it needs a logout on a real greeter, and if the 4.1 MB anon is live data (PAM, wlroots statics) this gains nothing. Moving the login screen into a child process so the waiting parent stays small would cut more but touches the PAM session ownership, so it was not done blind.
- Per-output desktop process: 3.0 MB PSS here (2.2 on the VM before this pass), 0.7 MB for the wallpaper process; merging them saves only the per-process baseline (under 1 MB per extra screen), not done.
- Looked at and left: the 4 MB wallpaper buffer (one full-screen XRGB; 16-bit would halve it but bands gradients), the compositor's pixman swap chain (2 full-screen buffers, inherent), bar libraries (libpipewire 268 KB is the only large one and is used).
- Outside research (searches, no paid APIs): little published. Confirmed ideas already used here: pixman renderer (Cage and others run on it), glibc trim/mmap thresholds, arena cap, jemalloc background purge; `PR_SET_THP_DISABLE` is not used: the dev VM has THP `always`, and the compositor shows 6 MB of huge pages, but they back its two full-screen 4 MB buffers that are fully written anyway, so turning THP off would save about 2 MB at most (a headless A/B showed 0 huge pages, so it could not be reproduced without restarting the real session). sway's `max_render_time` is a latency setting, not a CPU saver.

Exact breakdown, dev VM 1280x800, pixman, PSS in KB (anon / file-backed / shared memory):

| Process | Total | Anon | File | Shmem | Source |
|---|---|---|---|---|---|
| fleetwm (compositor) | 20362 | 8908 | 5098 | 6356 | `src/compositor/` (server, output, view, workspace, input, layer_surface, glass_*, titlebar, shadow, ipc_server) |
| fleetwm-bar | 6970 | 3188 | 3546 | 236 | `src/bar/` (main, tray, mpris, volume_source, spectrum_source) |
| fleetwm-desktop | 6127 | 2188 | 1899 | 2040 | `apps/desktop/main.cpp` |
| fleetwm-greet (root, tty1) | 7163 | 4144 | 3019 | 0 | `src/greeter/` (main, compositor, auth, session, login_ipc) |
| fleetwm-lockapplet | 1664 | 632 | 1032 | 0 | `apps/lockapplet/main.cpp` |
| fleetwm-wallpaper | 713 | 308 | 405 | 0 | `src/wallpaper/main.cpp` |

Shared by all of them: `src/fleetkit/` (cairo text, icons, widgets, the memfd surfaces named `fleetwm-kit`) and `src/common/` (config, malloc tuning). In the compositor, the shared part is 4080 KB of wallpaper buffer, 2276 KB of toolkit surfaces (counted half in each client), two 4 MB anonymous full-screen buffers (huge pages), and 7824 KB other anonymous data. Kernel: 40 MB unreclaimable slab, 6 MB page tables.

Research for very small machines (secondhand, no official figures for 128 MB desktops): Tiny Core needs 64 MB and Damn Small Linux 16 MB graphical, both with FLTK-class toolkits and no fontconfig; antiX reports about 57 MB idle on tuned old hardware; musl instead of glibc gives static programs of 9 KB against 431 KB and 128 KB thread stacks against 2-10 MB (Bootlin, openwall); zram sized to about 20% of RAM with priority above disk swap; `journald Storage=volatile` with `RuntimeMaxUse` and `RuntimeMaxFileSize` caps (journald was 43-46 MB on the dev VM); no data found for KSM, RGB565 framebuffers or kernel slab trimming. For Fleetwm the levers that follow are: a 16-bit render path (halves every full-screen buffer: 3 x 4 MB on this screen), fewer fonts loaded through fontconfig, and shipping a small-system profile (volatile journal, zram, no tailscale/Samba). None built yet.

### Realistic whole-system profile, real GPU, no tailscale (2026-10-26)
Fleetwm: compositor 71 MB PSS on real GLES2 (Celeron N4020 laptop, boot report `perflog/2026-10-08/ram-report-laptop-boot.txt`, older build, file-backed 48 = Mesa, anon 22; there is no memory report for the ThinkPad E490) + bar 6.8 + greeter 7.0 + desktop 6.0 + lock applet 1.6 + wallpaper 0.7 = about 93 MB. Services needed for every feature (dev VM PSS, `ram-report` of the same day): systemd 10.8, journald capped volatile about 8 (44.9 uncapped), wireplumber + pipewire + pipewire-pulse 16.5 (sound), udisksd 6.9 (mount/eject), udevd 4.0, dhcpcd 3.2 + wpa_supplicant 3.1 (network), polkitd 3.2, dbus 2.7, logind 2.4, user session 2.2, timesyncd 1.8, power-profiles 1.4 = about 66 MB, plus bluez about 3 (estimate). Kernel memory that cannot be given back: 75 MB on the Celeron laptop (67 slab + 8 page tables; 49 on the VM). **Idle desktop about 235 MB in use; each window opened adds 6-8 MB** (Settings 5.7, Task Manager 6.0, file manager 7.7). Roughly a fifth of that is clean library pages (Mesa in the compositor above all) that the kernel can drop under pressure. 512 MB is comfortable, 256 MB works with zram and a capped journal, 128 MB needs the changes in the list above (16-bit path, smaller font set, no polkit/udisks/avahi) and is not reached today.

### Alpine Linux, musl (2026-10-26)
Container `alpine:3.21`, headless compositor on pixman, 1280x720, 20 s idle, same start script on the Debian 13 host (glibc, jemalloc preloaded as in
the session); PSS includes shared libraries and is not comparable across the two (the host shares libraries with every other program), so private memory
(USS) is the fair column. USS, KB, Alpine vs Debian: bar 2412 vs 2732 (-12%), desktop 2292 vs 2512 (-9%), wallpaper 488 vs 736 (-34%); CPU ticks equal
(0-5 in 20 s). So musl saves 0.2-0.25 MB per small program and nothing else. **The compositor is the exception: 52 MB private on Alpine against 11 MB on Debian**, because
Alpine's `libgbm` and `libEGL` are linked to `libgallium`, which is linked to `libLLVM` (33 MB of file pages mapped, plus 6.6 MB of anonymous memory from musl's
eager relocation of it). Debian's `libgbm` does not do that. A real GPU needs the Mesa driver anyway, but the LLVM part only serves llvmpipe and radeonsi. A
pixman-only wlroots build (no EGL, no GBM, no Mesa) would not load any of it: on Alpine that is up to 41 MB, on the Celeron laptop it is the 48 MB of Mesa
files in the 71 MB compositor, and the notes already measured GLES2 at 9x the compositor CPU of pixman on that machine. Not built or measured yet; this is the
largest lever found for machines under 256 MB and for weak CPUs, at the cost of GPU-composited drawing.
Found by this port and fixed: `transfer.hpp` used `dev_t` without `<sys/types.h>`, `renameat2` and `RENAME_NOREPLACE` are missing in musl 1.2.5, `mallopt` and
`malloc_trim` do not exist there.



## Leaner round (2026-10-27)

Measured on the dev VM (Debian 13, 2 GB, pixman) with the compositor run nested in the live session (`WLR_BACKENDS=wayland`), PSS from
`/proc/<pid>/smaps_rollup`. The Celeron and E490 laptops were not reachable (192.168.0.182: no route), so every real-GPU figure below is still open.

| Change | Result | Evidence |
|---|---|---|
| Compositor drops its library code pages 10 s after start (`drop_code_mappings`, `src/common/page_release.hpp`) | Pss_File 2569 -> 1393 KB (-1.2 MB), RSS 28.6 -> 23.5 MB, still running 8 s later | nested run, `t=8` vs `t=13` |
| Greeter pages itself out while it waits for the session (`page_out_idle_process`) | with zram: 19.7 -> 3.3 MB RSS for a 16 MB heap, data intact after touching it again | `po.cpp` test program on a host with zram; the real greeter on tty1 was not run |
| Wallpaper buffer sent to zram after 10 s (`page_out_memfd`), only when a zram swap is active | not measured on the VM (its swap is a disk partition, so the code skips it); a solid colour is stored as a few bytes per page | static reasoning, zram same-page handling |
| `-ffunction-sections -fdata-sections -Wl,--gc-sections` | stripped compositor 1211960 -> 1027000 bytes (-15%); bar -15% and desktop -36% but those two compared against older builds | `size`, `strip` |
| Same flags, resident memory | no change beyond noise: compositor Pss 13578 vs 13667 KB at t=8 | nested run, b vs b2 |
| journald capped (`install.sh`): `SystemMaxUse=100M`, `RuntimeMaxUse=16M` | not measured | journald was 46.5 MB on the VM |

How it works and what it cannot do:
- `MADV_DONTNEED` on r-x file mappings only. It throws away the page-table entries, not the page cache, so the saving shows in RSS and PSS and
  the pages return with a minor fault when code runs again. `MemAvailable` does not rise until the kernel needs the RAM. Writable and read-only
  data mappings are never touched: RELRO pages are relocated copies and `MADV_DONTNEED` would put the unrelocated file bytes back.
- `MADV_PAGEOUT` on a library the user does not own is refused by the kernel, which is why the user's compositor uses `DONTNEED` and the root
  greeter can use `PAGEOUT`. Anonymous memory goes to swap, so both anonymous and memfd paging are gated on a zram swap being active
  (`has_zram_swap`); paging to a disk swap would cost a disk read per page.
- The greeter cannot exit after login: its process holds the PAM handle, and `pam_close_session` has to run in it at logout. Making it
  smaller while it waits is the part that works.
- The Mesa part of item 1 (48 MB of the 71 MB compositor on the Celeron) is the expected big win and is **not measured**: the VM has no
  GPU renderer (`Cannot create GLES2 renderer: no DRM FD available` when nested). Run the nested test on the laptop:
  `Pss_File` at 8 s and 13 s from `/proc/<pid>/smaps_rollup`.

Compositor anonymous memory, 8.9 -> 12.2 MB: **measured, not a malloc-heap problem.** The compositor run headless under jemalloc with
`stats_print` (glass on, 3 printing `foot` terminals, pixman, 1280x720, 30 s idle phase of `scripts/grab-stats-run.sh`, command
`COMP_WRAP="env LD_PRELOAD=libjemalloc.so.2 MALLOC_CONF=stats_print:true,stats_print_opts:gdaxe" SOCK_WAIT=1200 ONLYIDLE=30`): **2.16 MB live
(0.84 small, 1.32 large), 5.0 MB resident including 1.55 MB allocator metadata**, 7.8 MB mapped. So the 12 MB seen on the live VM session is
not allocations by our code: the unnamed 4 MB, 2 MB and 1.9 MB regions in its `smaps` are full-screen buffers (1280x800x4 = 3.9 MB: the pixman
swap chain and the glass backdrop), plus relocated library data. Both are inherent to a software-rendered full-screen compositor, and the
earlier "titlebar/glass caches add up to about 6 MB" guess was wrong for this workload (the caches are far from their caps). heaptrack on the
harness produced an empty file (the harness's cleanup ends the recording), so there is no per-call-site split; jemalloc's totals were enough
to close the question. The bar's heap is 0.96 MB and the desktop's anonymous memory 96 KB after pressure; their libraries are shared.

Not done, with the reason: merging the desktop and wallpaper processes (the wallpaper process is 0.9 MB PSS, so the ceiling is under 1 MB
per screen for a refactor of two programs); a startup function-order file (needs a profile build, and the compositor's file-backed share
is already 1.4 MB after the drop).


### Risky compiler and linker flags, and the memory helper (2026-10-27, later)

**Flags tried and rejected as a set.** Candidate set, one build (`b3`) against the same tree with only `-ffunction-sections -fdata-sections --gc-sections` (`b2`),
GCC 14.2, binutils 2.44, dev VM: compile `-fcf-protection=none` (Debian's GCC has it on by default: drops every `endbr64`), `-fno-plt`,
`-fno-semantic-interposition`, `-fvisibility-inlines-hidden`, `-fno-asynchronous-unwind-tables`, `-fno-math-errno`, `-fno-trapping-math`;
link `-Wl,-z,noseparate-code`, `-Wl,-z,norelro`, `-Wl,--build-id=none`. Stack protector, `_FORTIFY_SOURCE` and stack-clash protection are
already off in this toolchain (nothing to remove).

| Measure | b2 | b3 (all risky flags) |
|---|---|---|
| Compositor `commit_ms_avg`, idle / move / resize, 3 interleaved runs each (`scripts/grab-stats-run.sh ... --renderer pixman --size 1280x720 --terminals 3`) | 0.26-0.29 / 0.69-0.71 / 0.76-0.79 | 0.25-0.30 / 0.71-0.73 / 0.75-0.78 |
| Stripped compositor / bar / desktop (bytes) | 1027000 / 1558680 / 586568 | 1026920 / 1566872 / 585256 |
| Nested compositor PSS at t=13 s (file / total) | 1380 / 12304 KB | 1331 / 12503 KB |

Nothing beyond noise in speed, size or memory, so none of it is kept: the security flags (CET, relro, separate code) are worth more than zero.
`-ffast-math` was not built: the compositor's float work sits in cairo/pixman/wlroots, which this flag does not touch, and a NaN check in
config parsing is the one place it could bite. The earlier verdicts stand (LTO on, `-march=native` and `-fno-plt` rejected). Not tested on the Celeron.

**`fleetwm-memd` (built, measured, removed at the owner's request, 2026-10-27).** A 32 KB C helper that set up zram or zswap at boot and, on a kernel
PSI memory-pressure event, paged idle background processes out to the compressed swap. It worked on the dev VM (idle 200 MB test process to 6.3 MB
resident with zswap, 160 ms to touch it all back, 4.2 us a page; zram 173 ms; daemon 1.5-1.9 MB RSS, no CPU idle, about 0.23 s of CPU per
436 MiB swept) but it adds a resident process, a capability-carrying binary, a boot service and sysctl changes, and duplicates the kernel's own
reclaim, with a benefit only under real pressure. Not re-tried unless a measured case shows the kernel's reclaim thrashing.


### CPU and heap pass (2026-10-27, code only)

**Per-call-site heap split** (what the earlier note could not give). `scripts/grab-stats-run.sh` now takes `COMP_GRACE=SECONDS`: at the end it terminates only the
compositor and waits for the wrapped tool to write its file (before, the wrapper was killed with it). With valgrind massif it works:
`COMP_GRACE=120 COMP_WRAP="valgrind --tool=massif --massif-out-file=OUT --depth=30" SOCK_WAIT=2400 ONLYIDLE=40 scripts/grab-stats-run.sh build DIR --renderer pixman --size 1280x720 --terminals 3`,
then `ms_print`. heaptrack still writes an empty file for the compositor under this harness (it works on `ls`; cause not found), so massif is the tool.
Live heap at the end, 2.44 MB: `create_pixel_buffer` 810 KB (the screen-fixed glass ray 285 KB, title bar pictures 235 KB, the rest per-window frames),
the glass content read-back vector 326 KB (`glass_scene.cpp` `gs_paint_content`), the pixman glyph cache cairo creates 262 KB (fixed size, one per font
context), other pixman 211 KB, wayland-server 82 KB, libstdc++ 74 KB, fontconfig 57 KB. Nothing else is above 1.5%.

**Box blur: 2.9x faster, same bytes** (`src/fleetkit/box_blur.cpp`, used by glass backdrops, window shadows, caption glow, wallpaper blur). Callgrind of a
window drag over three repainting terminals (glass on, pixman, 1280x720) put 36.7% of all instructions in the blur's vertical pass and 3.9% in its
transpose. Two causes, both found by reading the compiler's output: the divide-by-multiply used a 32-bit multiplier, so the vector code did a 32-bit
multiply per lane (SSE2 has no cheap one) instead of the 16-bit multiply-high `pmulhuw`; with the multiplier typed `uint16_t` the compiler emits
`pmulhuw`. Once that pass was cheaper the transpose was 43% of the blur, so the 4-channel transpose now moves 4x4 blocks of pixels through SSE2
registers (scalar fallback for other CPUs and for the edges).

| 4-channel blur, radius 3, 3 passes, best of 9x200 (i5-8500) | before | pmulhuw | + SSE2 transpose |
|---|---|---|---|
| 160x85 | 212 us | 87 us | **72 us** |
| 320x180 | 905 us | 361 us | **300 us** |
| 480x270 | 2055 us | 838 us | **697 us** |

Output identical (same checksums at all three sizes; `tests/test_box_blur.cpp` compares against the original loops for every size, radius, pass count, padded rows
and 1 and 4 channels, passes). AVX2 (`-mavx2`) would give only another 1.17x and the Celeron has no AVX, so no `target_clones`.

In the compositor (dev VM, pixman, 3 terminals under a glass window, 3 interleaved runs each, `scripts/grab-stats-run.sh ... --rounds 1 --terminals 3`),
the time spent rebuilding glass drops with the same number of rebuilds per second: **idle 3.4 -> 2.4 ms/s, move 4.9 -> 3.6, resize 4.5 -> 3.1** (-27 to -31%,
every run). `commit_ms_avg` (the renderer, mostly pixman compositing of the terminals) is unchanged at 0.25-0.31 / 0.68-0.73 / 0.61-0.79, as expected.
What is left in that profile is pixman compositing (`pixman_image_composite32`, 28% of instructions, the three terminals being redrawn), which belongs to
wlroots' renderer and the scene; nothing in our code is above 7% any more (`glass_build_pictures` 6.4%, its 4x4 averaging).

**Memory.** The scratch buffers kept for ever are now given back: the glass read-back vector (grows to the biggest window seen, capped at 1.5 million pixels = 6 MB)
and the blur's working copies (two pictures plus a row of sums) are released 4 s after the last glass rebuild (`glass_scratch_trim`, `box_blur_release_scratch`).
Not demonstrated end to end: jemalloc totals with one Settings window showed no change (2.33 MB allocated in both, resident 4.52 -> 4.38 MB), and a larger-window scene
(`--win-scale`) did not run. The saving is expected only with big windows under glass; it costs one reallocation on the next rebuild after a quiet spell.
