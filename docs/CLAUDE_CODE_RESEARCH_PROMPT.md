# Claude Code research prompt: Fleetwm performance and reliability handoff

Use this prompt in Claude Code from the Fleetwm repository. It is a request for
an evidence-based review and a proposed next investigation, **not authorization
to edit code, alter the laptop, restart Fleetwm, or commit/push**.

---

## Prompt

You are continuing work on Fleetwm, a C++/wlroots Wayland compositor and
desktop shell. First read the repository's `CLAUDE.md` if present, the user's
Claude instructions at `/home/bean/.claude/CLAUDE.md`, the relevant Fleetwm
memory and handoff documents listed below, and the developer-performance-opt
skill. Treat older dated handoffs as historical; reconcile them against the
newest evidence rather than copying old “next step” lists forward unchanged.

### Objective

Analyze the current evidence and research the most promising next work to make
Fleetwm responsive and low-resource on older systems, especially the Celeron
N4020/UHD 600 laptop, without sacrificing support for Intel iGPU/dGPU, AMD
iGPU/dGPU, NVIDIA dGPU, or other supported GPUs. Assess the unresolved
Chromium launch incident separately from compositor performance. Return a
prioritized, falsifiable investigation plan to the owner; do not implement it.

The user's aim is strong responsiveness and minimal GPU/system-memory use, not
an excuse to disregard visual behavior, portability, correctness, or evidence.
Do not promise “the fastest” or infer a gain from code inspection alone.

### Read these inputs

Read current repository state and these files before forming conclusions:

- `docs/NEXT.md`
- `docs/OPTIMIZATIONS.md`
- `docs/PERFORMANCE_FINDINGS.md`
- `docs/PERF_LOG_2026-10-07.md`
- `docs/PERF_LOG_2026-10-11.md`
- `docs/HANDOFF_2026-10-08.md` (historical; check whether each item was later
  completed)
- `/home/bean/HANDOFF_2026-10-10.md`
- `/home/bean/HANDOFF_2026-10-11.md`
- `/home/bean/ai-handoff-summary.md`
- `/home/bean/fleetwm-next-session.md`
- `/home/bean/.copilot/session-state/15c0c027-4e54-4807-9a89-ea025243b4db/plan.md`
- `/home/bean/.claude/CLAUDE.md`
- `/home/bean/.claude/projects/-home-bean-git-fleetwm/memory/MEMORY.md` and its
  linked Fleetwm memory files, especially `fleetwm-test-machines.md`,
  `reference-performance-playbook.md`, and the commit/build safety feedback
- `/home/bean/.claude/skills/developer-performance-opt/SKILL.md` and relevant
  references: `case-desktop-compositor-field-notes.md`,
  `benchmarking-protocol.md`, `non-default-flags-checklist.md`,
  `linux-methodology-and-tracing.md`, and `measurement-science-memory-and-systems-sources.md`

Also inspect the current source paths relevant to any conclusion, including
`src/compositor/output.cpp`, `src/compositor/server.cpp`,
`src/compositor/input.cpp`, the taskbar/bar and Task Manager process/UI paths,
the wallpaper client, GPU monitor backends, and associated tests. Verify
whether implementation or hardware state has changed since these notes were
written. Do not assume an absolute PID remains current.

### Evidence and history to preserve

Use the newest measured facts as starting points, while checking their source
and caveats:

- The target is `dev@192.168.0.182`, host `uefi-x86`, Celeron N4020, Intel UHD
  600/i915 UMA, 3.4 GiB RAM, 1920x1080 eDP. This is one reference machine, not
  the design limit or a basis for Intel-only policy.
- The verified post-cap drag A/B is a **nested 1280x720** test with three
  balanced pairs. Full-capture per-process Render/3D medians were 14.45% with
  glass and 13.85% flat; paired deltas were +0.21 to +1.18 percentage points.
  Paired compositor-plus-bar task-clock results were mixed (medians 501.10 vs
  497.80 ms over seven seconds). Individual 500 ms samples peaked in the
  mid-40% range. The test does not measure frame deadlines, input-to-present
  latency, or the user's exact 1920x1080 foreground workload.
- Do not confuse aggregate task-clock with rendering latency. For example,
  501 ms task-clock over a seven-second capture is total CPU time accumulated
  over that interval, not a 501 ms frame.
- Task Manager's measured duplicate-row-copy removal reduced idle-visible
  client task-clock by about 3.4% across three paired samples. This is a modest
  CPU-work reduction, not proof of lower input latency or GPU usage.
- A six-second idle i915 snapshot reported about 62 MiB compositor-client
  tracked/resident system-memory objects. This is shared memory, not dedicated
  VRAM, not a complete allocation inventory, and not a workload high-water
  mark. Wallpaper-size reduction is only a candidate until the visual and
  per-backend memory effects are measured.
- Fleetwm's drag/move resize composition is capped at 24 FPS in the current
  implementation, while fullscreen remains uncapped. Inspect how scene
  mutations, pending damage, frame callbacks, resize configure requests, and
  client redraws behave under that cap before proposing coalescing or another
  cap. Do not assume fewer pointer updates automatically mean less rendered
  work or better latency.
- Current source trace: `Server::update_grab()` in `src/compositor/server.cpp`
  sets the scene-node position on each move event; resize also updates the
  node, but only requests a new client size when its dimensions change.
  `output_frame()` in `src/compositor/output.cpp` enforces the interactive
  24-FPS limit, skips a not-yet-due frame only when there is no pending damage,
  and sends client `frame_done` after a commit. These code paths identify
  counters to measure, not proof that motion-event-rate scene updates are a
  bottleneck. The project targets the wlroots 0.18 series; validate API claims
  against the version actually built, not current online documentation.
- Three Chromium Crashpad dumps from 20:08:31, :39, and :47 showed SIGSEGV in
  `libxkbcommon.so.0`'s `xkb_state_update_mask` with a null first argument in
  Chromium's Wayland/Ozone path. Later controlled launches did not reproduce
  it. A real taskbar-pin launch using the default profile opened Chromium and
  it remained open for at least 2:36 with no new dump. This is a successful
  attempt, **not a root-cause fix**; do not assert Fleetwm caused the dumps or
  that the issue is resolved.
- Do not label Intel's RC6-derived Task Manager metric as render-engine busy.
  Use `intel_gpu_top` Render/3D for Intel engine measurements. AMD and NVIDIA
  counters have different source semantics and sampling windows.

### Research questions

1. What one or two experiments most directly identify why the user's full
   1920x1080 workload uses substantial GPU/CPU, and what signal would prove or
   falsify each hypothesis?
2. Which work is application/UI-side versus compositor scene update,
   composition, GPU execution, driver/client upload, or instrumentation
   overhead? Separate window move from resize and idle from active interaction.
3. Is there an implementable, low-overhead way to capture per-frame damage
   area, scene-position update count, commit/render timing, missed presentation
   deadlines, and input-to-present latency? State what wlroots exposes in the
   repository's actual pinned version, what can be read without patching
   wlroots, and the limitations of each proxy.
4. What workload and counters are needed to measure memory high-water marks
   under multiple windows, workspace switching, window switcher, and fullscreen
   scanout? Distinguish process RSS/PSS, client buffers, driver-tracked
   allocations, dedicated VRAM, and shared/system memory; never compare
   unlike counters as if they were equivalent.
5. For the Chromium crash, what next *non-invasive* capture would distinguish
   the normal taskbar launch path, profile state, startup flags, Wayland
   keyboard/keymap/modifier sequencing, and timing? The actual browser profile
   and live desktop must remain untouched unless the owner explicitly approves
   a narrowly described experiment.
6. Check applicable build/runtime flags against the skill's non-default flag
   checklist. Include safe candidates, rejected measured flags, and changes
   requiring owner consent; do not cargo-cult CPU ISA flags, shader-cache
   settings, triple buffering, or GPU-vendor-specific workarounds.
7. Reconcile the historical open-item lists. Report only work that remains
   open today, with the newest source of truth and the reason older notes are
   stale.

Use primary sources where possible (actual pinned wlroots headers/source,
vendor/kernel documentation, upstream Chromium source for the installed
version, peer-reviewed papers when relevant). Cite a link or exact local
file/line for each technical claim. Distinguish project measurements, source
inspection, external evidence, and speculation. External examples are methods
or hypotheses, not Fleetwm measurements.

### Safety and owner rules

- This is analysis only. Do not edit files, change configs, install packages,
  mutate the target machine/profile, alter system flags, run an invasive
  benchmark, restart the live window manager, or reboot. If you need such an
  action, describe the exact scope, risk, and expected evidence and wait for
  explicit approval.
- Never kill by broad name/prefix or use `pkill -f`; capture and use exact
  child PIDs for any future approved disposable test.
- Do not expose passwords, tokens, private SSH configuration, or other
  credentials in reports, prompts, commits, or pushed files. The target's
  address/user may be mentioned only as needed; never repeat a password.
- No speculative source edits. Any eventual optimization must use a
  representative workload, same-machine interleaved A/B runs, environment and
  build details, distributions/spread, and correctness checks. Report CPU,
  GPU, frame cadence/latency, and memory separately.
- If a future change is proposed, preserve behavior and test at 1024x768.
  Keep changes backend-neutral unless a capability-tested path is justified.
- Commits require a separate explicit owner approval. Use one-line
  `type(file): message` and no AI co-author/sign-off/trailer. Do not push
  without explicit approval.

### Deliverable

Return a concise but evidence-dense report with:

1. **Executive finding:** what is known and what remains unproven.
2. **Evidence ledger:** claim, source, date, measurement type, confidence,
   caveat, and whether any newer note supersedes it.
3. **Reconciled open work:** ranked and deduplicated, not a copy of stale
   backlogs.
4. **Top next experiments (maximum three):** hypothesis, minimal safe setup,
   exact signals/commands or instrumentation points, repetitions/control,
   pass/fail interpretation, risks, and owner approval needed.
5. **Potential source changes:** only as conditional proposals tied to
   measurable outcomes; do not implement.
6. **External references:** primary links and exactly which inference each
   supports.

Finish by stating plainly whether any fix is currently justified. Stop and
wait for the owner's choice before making changes.

---

## Current handoff snapshot

This prompt was assembled on 2026-10-11. The detailed dated home handoffs
remain the authoritative record for raw history; the repository performance
logs hold the measurements. After the next investigation, update the dated
handoff, `docs/NEXT.md`, `docs/PERF_LOG_2026-10-11.md` or a more suitable
focused log, the AI summary, and the active session plan together. Never copy
credentials into this prompt or public repository documentation.
