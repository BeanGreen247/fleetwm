# Performance findings carried over from Lestrix, for the next Fleetwm round

Merged on 2026-10-07 from three sources: the Lestrix performance write-up of 2026-10-07 (`lestrix/native/docs/PERFORMANCE_2026-10-07.md`), the notes that were copied from Fleetwm into the Lestrix tree on 2026-10-06 (`fleetwm-performance-notes.md`, folded in below), and the general lessons in the owner's optimization notes. `OPTIMIZATIONS.md` in this folder stays the record of what Fleetwm itself has done; this file says what the Lestrix round learned that should be tested on Fleetwm next.

Nothing in the Lestrix sections has been measured on Fleetwm. Every item is "test whether this applies", with the measurement to take first.

## 1. Method (what paid off in both projects)

- Interleaved A/B runs, same session, same machine, alternating the two builds, best-of-N or median, `perf stat -e task-clock` for CPU seconds (wall time alone hides extra CPU). Differences under about 5% are noise. Compare only like with like.
- Measure on the weak machine. A VM showed 9% for something that cost 58% on the Celeron laptop.
- Idle baseline first: CPU ticks over a fixed window, wakeups per second per thread (`voluntary_ctxt_switches` + `nonvoluntary_ctxt_switches` in `/proc/PID/task/*/status`), RSS and PSS (`smaps_rollup`).
- Check the instrument before the system: every overlay, FPS counter and debug draw off, measure, then on. Fleetwm's overlay was the main cost on the laptop.
- Upper-bound experiment before building anything: delete the suspect work entirely (for example skip the history copy) and measure. If the wall time barely moves, stop; if it drops by a third, the work is worth attacking. This is how Lestrix chose the compact-scrollback work (1.18 s fell to 0.81 s with the copy free).
- Profile the real binary with `perf record -F 5000 -g --call-graph fp` on a `-g -fno-omit-frame-pointer` build and read it per thread (`--sort comm,sym`). Name the threads (`prctl(PR_SET_NAME)`) so the report is readable. Needs `kernel.perf_event_paranoid=1` (`sudo sysctl -w kernel.perf_event_paranoid=1`, resets on reboot).
- Per-phase timers in a throwaway build (clock_gettime around each stage, print averages), then restore the source.
- Estimate the speed of light first (section 3). A 3x gap is structure, not instruction tuning.
- Wrong assumptions found this way: "memory-bandwidth bound" turned out to be 15-30% of RAM bandwidth; "compression is cheap" turned out to be 44% of all CPU in a flood because most compressed work was thrown away.
- Never `pkill -f` a path prefix on a machine someone is using; kill by the recorded PID. Drive tests with in-app scripts and keep temp files out of RAM-backed /tmp.
- GPU load is separate from CPU load: `intel_gpu_top -J -s 1000` (needs root) gives render busy %, clock and RC6 residency.

## 2. The findings, ranked by how likely they transfer to a compositor/shell

### 2.1 Work that is done and then thrown away (biggest single win in Lestrix)

Lestrix compressed every scrollback block as it was created; with the default 10,000-line limit almost all of them were evicted soon after, so compression was 44% of all CPU in a flood. The fix is to defer: keep new blocks raw in a bounded tail (16 MB or half the RAM budget) and compress oldest-first only when the tail overflows or the program is idle; an evicted block just drops its job. 256 MB flood: 1.33 s to 1.13 s wall, CPU 4.9 s to 2.6 s on 8 threads, 3.2 s to 1.6 s on one CPU (2x). For Fleetwm: look for cached or precomputed things whose results are discarded before use (glass tiles rebuilt and evicted, icon decodes, thumbnails, layout results for windows that change again before paint). Count produced against consumed.

### 2.2 Redraw only when something can have changed

Pointer motion redrew the whole window on every event. Fix: while building a frame, log every rectangle that was tested against the pointer position; on a motion event compare the old and new position against that log and repaint only if some rectangle changed sides (always repaint while a button is down or a menu is open, or if the log overflowed). 100 real moves over the terminal: 13 frames in total instead of one per event. For Fleetwm: the same trick answers "does this pointer event change anything we draw" for the bar, start menu and titlebar buttons, without a per-widget dirty flag.

### 2.3 Fewer bytes per item

Compact scrollback lines (1 byte per ASCII character instead of an 8-byte cell, expanded on first read, with the cell form kept for anything else and an adaptive switch so mixed data does not pay): ASCII log 256 MB 0.50 s to 0.31 s. Lossless, verified line by line against the serial parser under ASan/UBSan. For Fleetwm: any retained per-item structure that is mostly one common case (window list entries, icon entries, theme tokens, glass tile keys) can keep a small form for the common case and expand on demand.

### 2.4 Overlap the serial stage with the parallel stage

The parallel parser now works in pipelined batches (the IO thread commits batch N while workers parse batch N+1) and no longer copies the input through a second buffer to convert line endings. Random text 1.12 s to 0.93 s, ASCII 0.27 s to 0.21 s. Smaller cuts on the serial thread (escape pre-scan only after a stop, limit checks every 128 lines, zero-copy blocks) took ASCII to 0.18 s. Rejected: bigger feed slices, moving the history copy to the workers (more total CPU, no wall gain), more compress or parse threads (the serial thread is the limit), a shorter `sched_yield` spin.

### 2.5 Kernel interfaces: pty and pipes

- The kernel's N_TTY output post-processing (OPOST/ONLCR) handles every newline character by character and delivers tiny reads. A pty with it on does 0.04-0.09 GB/s on line text; with it off 0.41-0.46 GB/s, regardless of read or write size (`native/bench/ptybench.c`). Lestrix switches it off only while a known plain printer is in the foreground, restores it with a pidfd watcher the instant the job exits, and has the terminal add the carriage returns itself. Plain `cat` of 256 MB through the pty: 3.05 s to 0.95 s. Pitfall found by test: readline saves and restores termios around every prompt and will keep a switched-off state forever if the prompt starts before the restore.
- A pipe feeding a helper gets a 4 MB buffer with `fcntl(fd, F_SETPIPE_SZ, 4 << 20)` before reading: `cat file | cat` 1.47 s to 0.40 s. For Fleetwm: any pipe or socket between the compositor, bar and helpers that carries bulk data (screenshots, clipboard, wallpaper frames) is worth the same one-line test; check `/proc/sys/fs/pipe-max-size` and `SO_SNDBUF`/`SO_RCVBUF` for Unix sockets (Fleetwm's measured IPC round trip is 37 us median; bulk transfers were not measured).
- Bypassing the kernel path entirely is what `lxcat` does for `cat FILE` (data through a file in /dev/shm, only a short escape sequence through the pty).

### 2.6 CPU governor

Same binaries, same session, `powersave` (intel_pstate, about 3.4 GHz) versus `performance` (3.7 GHz): floods 7-12% faster, RAM write bandwidth unchanged, single-thread RAM read +35%, kernel pty path unchanged. Only `powersave` and `performance` exist under intel_pstate. Record the governor next to every benchmark (the Lestrix `native/benchmark` summary now starts with a Machine line). The Celeron N4020 in the Fleetwm laptop should be tested the same way before trusting any frame-time number; Ansible's `setup-kernel-perf-tuning.yml` already detects VM versus bare metal and picks the governor.

### 2.7 Closed doors (measured, no gain, do not retry without a new reason)

- Compiler and link flags on a memory-bound engine: `-flto`, `-O2`, `-mtune=native`, `-march=native`, `-funroll-loops`, alignment flags, `-fno-plt`, `-fno-pie -no-pie`, `-fcf-protection=none`: best-of-12 identical.
- Profile-guided builds: GCC instrumented PGO with `-fprofile-partial-training` no gain; AutoFDO not possible here (Debian autofdo 0.19 cannot read perf 6.12 data); BOLT 19 (`-Wl,-q`, `perf record -j any,u`, ext-tsp, hfsort, split-all-cold) no gain, binary 8x larger. Recipe notes: the `perf2bolt-19` symlink is not recognised and writes a rewritten ELF, use `llvm-bolt-19 BIN -p perf.data -aggregate-only -o fdata`; only 24% of functions had any profile.
- LZ4-style skip acceleration in the scrollback compressor (no speed-up), writing parsed rows in place (noise), malloc trim / top-pad / mmap-threshold changes (no change), shorter or removed `sched_yield` spin, helper threads for rendering (earlier round). Fleetwm's own list: `MESA_NO_ERROR=1`, `mesa_glthread=true`, paging out Mesa's libLLVM, prewarming apps.

## 3. Hardware limits of the development laptop (Intel i5-8265U, 4 cores / 8 threads, 40 GB RAM)

Measured with `lestrix/native/bench/membench.c` (GB/s, per-thread buffers, threads pinned one per core):

| Test | 1 thread | 2 | 4 | 8 |
|---|---|---|---|---|
| memcpy in L1 (16 KB), read+write traffic | 192-203 | 302-384 | 193 | 178 |
| memcpy in L2 (128 KB) | 72-85 | 153-167 | 128 | 121 |
| memcpy in L3 (2 MB) | 53 | 36-48 | 22-25 | 22 |
| memcpy in RAM (64 MB) | 25.7-27.1 | 25.7 | 25 | 24 |
| RAM read (AVX2 sum) | 14.2 / 19.1* | 19.9 / 24.3* | 21.5 / 24.7* | 19.4 / 24.7* |
| RAM write (memset) | 27-29 | 27-31 | 28-29 | 27-28 |

\* `powersave` / `performance`. RAM gives about 25 GB/s of copy traffic (12.5 GB/s of data copied); one thread cannot read faster than about 14-19 GB/s; shared L3 bandwidth collapses once four or more threads use it. The Celeron N4020 (2 cores, single-channel memory) will be much lower: run `membench` on it first and write the table next to this one, because every speed-of-light estimate for Fleetwm's copy-heavy paths (client-pixel upload was a third of compositor time: `rep movsb` in memcpy) starts there.

What it said about Lestrix: the ASCII flood used 15-25% of RAM bandwidth and the random-text flood about 30%, so neither was memory-bound; the limit was one serial thread and L3 contention between the parse threads.

## 4. Fleetwm results to compare against (from `OPTIMIZATIONS.md`, 2-core Celeron N4020, UHD 600, 1080p)

| item | result |
|---|---|
| overlay on, desktop idle | 58% to 0.27% CPU after making it one picture |
| overlay on, busy terminal | 37.8% to 6.5% |
| one overlay repaint | 1.1 ms to 0.55 ms |
| bar clock tick, glass on | 0.72% to 0.28% CPU |
| titlebar hover redraw | 342 us to 51 us with cached pictures |
| IPC round trip over a unix socket | 37 us median |
| scrolling full-window terminal | compositor 1.1 ms CPU per frame, GPU 30% busy |

## 5. Carried-over checklist from the first copy (2026-10-06), still open for Fleetwm

1. Idle redraw loop: does anything redraw or swap buffers while nothing changed (cursor blink, status line, debug overlay)? Look for update-after-present patterns that cause the next frame by themselves. Test idle CPU and context switches per second with and without each visible feature.
2. Overlay and FPS counter cost: draw as one small texture, refresh a few times a second inside a frame that happens anyway, allocate only while visible.
3. Redraw only what changed: damage rows or rectangles; check partial present (EGL buffer age, `EGL_KHR_partial_update`) on the target drivers; at minimum skip presenting when nothing changed.
4. Cache finished pieces (panels, bar segments as textures keyed by content, byte-capped, keeping a reference to what the key points at).
5. Upload cost: the CPU copy of client pixels to the GPU; upload only the damaged region and measure the per-frame upload while scrolling (4 MB x 60 fps = 250 MB/s on a 1080p full-window update).
6. Thread split: on a 2-core machine count context switches before adding threads (`mesa_glthread=true` saved about 5% and was not worth it). The Lestrix result agrees: threads help only until the serial stage is the limit.
7. Cheap stats: `getrusage(RUSAGE_SELF)` (one syscall) instead of parsing `/proc`; read memory and clock speed once a second, not per repaint.
8. Start-up CPU and decode-once work (fonts, icons, config).
9. Cache what is redrawn on every hover (titlebar redraw 342 us to 51 us with per-state pictures); test the cached picture equals the drawn one in every state and that the caches stay bounded.
10. Partial repaint of the seconds clock (0.72% to 0.28%): copy the last frame, clip, redraw, damage only that rectangle, full redraw when the layout moves.
11. A profile-guided build is only as good as its training run (20 s mostly idle was useless; 150 s across both layouts, glass on and off, every settings page, menus and input is not). Given the Lestrix PGO/AutoFDO/BOLT results, measure PGO again on Fleetwm's compositor before keeping its build complexity.
12. Static check before a long compile (unity-build name collisions), one instance of a popup (toggle, do not stack), idle wakeups per thread, and sound that exists but is silent (`alsactl init`, verify with a tone and the built-in microphone; see `AUDIO.md`).

## 6. Suggested order for the Fleetwm round

1. Install the profiling tools on the dev VM and the laptop (`linux-perf perf-tools-unstable autofdo bolt-19 gdb`; already in Ansible `dev_packages` and in Lestrix `install.sh --deps`), set `kernel.perf_event_paranoid=1`.
2. Run `membench` and the governor comparison on the Celeron laptop; record the governor with every number from here on.
3. Idle baseline per thread (compositor, bar, launcher, wallpaper): CPU ticks, wakeups, RSS/PSS.
4. `perf record -g` of the compositor while scrolling a full-window terminal and while idle with glass on; look first for work that is produced and discarded (2.1), redraws without a change (2.2), and copies (2.3).
5. Try the one-line IPC buffer test (2.5) and the pointer-motion repaint log (2.2) on the bar and start menu.
6. Re-measure each change with an interleaved A/B on the laptop; keep rejected results in `OPTIMIZATIONS.md` so they are not retried.

## 7. Lestrix round 2026-10-08 (findings that transfer; numbers are Lestrix's, not Fleetwm's)

Source: lestrix `native/docs/PERFORMANCE_2026-10-07.md` (sections "Serial parser", "Gap 1", "Gap 3" to "Gap 6") and `native/docs/PERF_GAPS_2026-10-08.md`; machine: i5-8500 desktop, 6 cores, perf blocked (`perf_event_paranoid=3`), so callgrind plus interleaved A/B builds.

1. **Delete the check, not just the copy.** Scrolled rows were packed from 8-byte cells to 1 byte per character with an SSE2 pass (+14%); marking the row "plain ASCII, one style" at write time and copying shadow bytes (+20-25%) beat even the upper-bound experiment (delete the pass, fill with a constant: +14-21%). A mark set by one writer and cleared by all others needs a differential test (same random stream with and without the optimisation, plus a mutation check that a deliberately broken variant is caught). Same idea applies to any "row/tile is clean and simple" flag in a damage tracker.
2. **Upper-bound experiment before the real change** (replace the suspect work with a constant and time it) decides in minutes whether a gap is worth chasing.
3. **Instruction count can rise while time falls**; judge by interleaved wall time, and always run the short-input case too (a 20-character-line regression of 20% was hidden behind the long-line win; fixed with a length threshold).
4. **Compiler flags again measured nothing:** `-march=native`, LTO, PGO, `-freorder-blocks-algorithm=simple`, `-fvect-cost-model=unlimited` were all inside noise or slightly worse on the parse gate (1369-1405 vs 1426 MB/s). A wider SIMD loop (16 bytes per step) was also no gain and cost short runs 10%.
5. **A hot loop's neighbours matter:** handling `\r` and `\n` inline right after a text run instead of re-entering the state machine gave +10-20% on 20-170 character lines.
6. **When a producer is idle, the bottleneck is downstream.** A timing build of the pipe helper showed it waiting 0.25-0.46 s of a 0.55 s run; replacing its sleep polling with `inotify` changed nothing and was reverted. Measure each stage's idle time before optimising a hand-off.
7. **`splice()` instead of read+write through a user buffer** for pipe to tmpfs: stage 0.22 to 0.17 s, end to end -4 to -9%. Overwriting two persistent tmpfs files instead of creating and unlinking per chunk would cut the stage to 0.11 s but needs an acknowledgement channel.
8. **Quadratic queue drains hide in small code:** `memmove` of the whole remaining queue after each partial `write()` (8 MB through a 4 KB pipe: 0.42 s, head offset 0.07 s). Look for this in IPC buffers and input queues (checklist 2.5).
9. **A `sched_yield` spin that looked wasteful measured as noise** (limits 2000/200/0 all 0.32-0.38 s); record such rejections so they are not retried.
10. **Test harness rules:** run GUI benchmarks and smoke tests under `xvfb-run -a` (never on the owner's display); `SDL_TEXTINPUT` events carry at most 31 bytes, so scripted typing must use short commands; software GL under Xvfb makes absolute numbers comparable only with each other.

Open on the Lestrix side that may matter here: per-request cost of streamed input on the consumer side (0.5 s per 256 MB as pipe chunks vs 0.30 s as one file), and whether widening the list of programs allowed to switch the pty to raw newlines is acceptable (owner decision).

## 8. Update 2026-10-08 (later): the rest of the Lestrix round, and the TEST LIST for Fleetwm

Lestrix is finished (commits bb77cac, 51f9cd7, 9af718a; notes in its `native/docs/PERFORMANCE_2026-10-07.md`, `PERF_GAPS_2026-10-08.md`, `terminal-io-diagram.html`, README rows 36-40). New results since section 7: widening the fast-output program list gave -25% on a line-buffered flood (24.2 to 18.2 s per 256 MB); the 200 us read-coalescing delay is on a plateau (0 us: 30 s, 50 us: 21 s, 100-400 us: 18-19 s, 1000 us: 21 s); real pty reads average 114-612 bytes, so small-chunk paths matter more than bulk paths; reusing a 1 MB stream buffer, an `inotify` hand-off, a `sched_yield` spin change, `-march=native`/LTO/PGO and a wider SIMD loop were all inside noise.

Fleetwm is a window manager and desktop shell with its own code, so ideas that did not pay off in a terminal, or that could not be tested there, may still pay off here. Nothing below has been tried in Fleetwm; each item says what to measure and the evidence gate applies (interleaved A/B on the laptop, record the governor, keep rejections in `OPTIMIZATIONS.md`).

| # | Idea (where it came from) | What to try in Fleetwm | Measure |
|---|---|---|---|
| 1 | Render side not measured (Lestrix gap 10: Xvfb software GL says nothing about a GPU) | Per-frame CPU and GPU time of the compositor, bar, launcher on the real GPU; dirty-region sizes per frame | CPU ticks per frame, damage area, frame time p50/p99 |
| 2 | "Row is simple and clean" flag set by one writer, cleared by all others (shadow rows: +20-25%) | Same pattern for the bar, titlebars and menus: mark a drawn piece as "unchanged since last picture" at the source so the repaint is a copy | repaint time before/after; differential test (same input with and without the flag, compare pixels) plus a mutation check |
| 3 | Compiler flags (`-march=native`, LTO, PGO, `-freorder-blocks-algorithm=simple`, `-fvect-cost-model=unlimited`) were noise on the Lestrix parser | Fleetwm already ships PGO (0.4.0); re-test `-march=native` and LTO on the Celeron laptop where the code is branchy C with wl_list walks, not a tight loop; record numbers | compositor idle and scroll CPU, start-up time |
| 4 | `inotify`/event wait instead of poll+sleep (no gain in Lestrix because the consumer was the limit) | Check Fleetwm for any sleep-and-poll hand-off (wallpaper, bar, tray, config reload); replace only where the poll shows in the wakeup count | wakeups per second per thread (idle baseline step 3 of section 6) |
| 5 | Read-coalescing delay (200 us is on the plateau; 0 us is 65% slower on small-chunk floods) | Event batching for client message floods (many tiny `wl_surface` commits, pointer motion): a short delay after a tiny read before the next read | syscalls per second, CPU under pointer-motion flood |
| 6 | Quadratic `memmove` queue drain (8 MB: 0.42 s to 0.07 s with a head offset) | `grep` Fleetwm for `memmove` after partial `write`/`send` in IPC out queues (bar, launcher, wallpaper sockets) and fix with a head offset | micro-benchmark of the queue; large clipboard or screenshot transfer |
| 7 | `splice()` pipe to tmpfs, kernel to kernel (-4 to -9% end to end) | Screenshot, clipboard and wallpaper hand-offs that read a pipe/socket into a buffer and write it out | transfer time and CPU for a 20-100 MB payload |
| 8 | `mmap` of a producer's file was rejected in Lestrix (SIGBUS if truncated) | Where Fleetwm reads a file it owns (icons, themes, config), `mmap` is safe; try for large icon/theme caches | start-up time, RSS/PSS |
| 9 | Program-name whitelist to switch a mode (fast output: only listed printers) | Same trade for any Fleetwm per-client fast path: a conservative list first, extend with evidence | per-client frame cost |
| 10 | Stream consumer lead left open: 0.5 s per 256 MB as pipe chunks vs 0.30 s as one file, not explained by thread count or buffer reuse | Check Fleetwm's own chunked consumers for per-request fixed cost (open/fstat/malloc/lock/wake per message) | per-request time vs per-byte time |
| 11 | Key-to-pixel latency was never measured (needs a GPU session) | With a high-speed camera or compositor timestamps: key press to first changed frame, with and without the 200 us coalescing delay | latency p50/p99, hard rule: no typing lag |
| 12 | Lestrix `--lite` and bar-less mode, render-on-change, fixed-slot stats bar | Run Lestrix under Fleetwm on the laptop: compositor cost while Lestrix floods, idle, and with `--lite`; check frame callbacks and damage reporting from an SDL2/GL client | compositor CPU, client fps, damage rectangle size |
| 13 | perf is blocked on the Lestrix desktop (`perf_event_paranoid=3`); callgrind and A/B builds were used | On the laptop and dev VM it is 1 already: use `perf record -g` per thread on the compositor instead of instruction counts | flame graph of idle and scroll |
| 14 | 1024x768 is the minimum supported resolution | Re-check every Fleetwm menu, popup, settings page, greeter and bar at 1024x768 alongside any change above | screenshots at 1024x768 |

Lestrix-side notes worth reading before the Fleetwm round: the method summary in the skill reference `developer-performance-opt/references/case-terminal-engine-and-io-path.md` and the byte-path diagram (`native/docs/terminal-io-diagram.html`).

## 9. Results of the section 8 test list (round 2026-10-08)

Rig for this round: dev VM `fleetwm-dev` (192.168.0.223), KVM guest, 2 vCPU Intel i5-8500, 2 GB RAM, pixman renderer, no GPU, no cpufreq governor exposed (guest), Debian 13, wlroots 0.18.2. `kernel.perf_event_paranoid` was 3 (reset by reboot) and was set to 1 at runtime with the owner's approval; not persisted. The Celeron laptop (192.168.0.182) was unreachable, so everything that needs a real GPU or the Celeron is marked open. Profiling build: `rig/mk.sh` natively on the VM, no `-march`, `-g -fno-omit-frame-pointer`, LTO + unity, not stripped, not PGO. Raw output: `perflog/2026-10-08/`.

| Row | Status | Result |
|---|---|---|
| 13 perf on VM | done | Idle compositor + bar: 3.56 ms task-clock over 30 s (0.012%), 85 context switches in 30 s (about 3/s). `perf record -g` of the idle compositor (24 samples in 20 s) is flat: the top symbol is 10.6% `syscall_return_via_sysret`, then pixman region tests and wlroots surface lookups at 5%. Scrolling full-window `foot -e yes` (367 samples in 20 s): kernel 37%, libwlroots 25%, libwayland-server 17%, libc 8%, libffi 5%, pixman 4%, fleetwm 3%; no symbol above 4% (`fdget` 3.6%, `wl_event_loop_dispatch` 3.3%, `output_frame` 2.0%). The compositor has one thread. This differs from the laptop profile (53% Mesa shm upload memcpy), because the VM has no GPU and the guest has a different copy path; so the laptop result stands for GPU machines and the VM shows the CPU-only picture: flat, nothing to delete. |
| 1 render CPU/GPU per frame on real GPU | OPEN | needs the laptop (down) or the owner's Lestrix GPU numbers; none pasted yet |
| 6 memmove queue drain | done, kept | No out queue exists (every `send` is one whole reply, no partial-write buffer). The read side is the same pattern in reverse: `IpcServer::handle_client_readable` and `IpcClient` erased the line from the front of the string per line, and the server reads until EAGAIN, so a flood is quadratic. Micro-benchmark (`g++ -O2`, 27-byte lines, median of 5): 4 KB 7 us vs 4 us; 256 KB 21.9 ms vs 0.23 ms; 1 MB 503 ms vs 0.92 ms. Both now walk with a head offset and erase once. Realistic traffic (a few lines per recv) is unchanged. 712 unit tests pass; the compositor compiles on the VM. |
| 7 splice in clipboard/screenshot/wallpaper paths | nothing to do | Fleetwm owns no such path: the clipboard is `wlr_data_device_manager` (the compositor passes fds between clients, never copies the payload), there is no screenshot code, and the wallpaper process reads one image file. No read-then-write loop to replace. |
| 4 poll-and-sleep hand-offs (wakeups/s) | rejected, no change | Every hand-off is already event driven: compositor and wallpaper use inotify + `poll(-1)`/wl event loop, the bar and `fleetkit::App` use `poll(-1)` over timerfds, the lock applet polls with the bus timeout. The only timers are the bar's (stats 2 s, disk 5 s, network 5 s, battery 15 s, clock) and the wpctl fallback (popen every 5 s when native PipeWire is not available; children are not in the numbers below). Upper bound (`perflog/2026-10-08/barab.sh`, interleaved 6 x 60 s, bar alone against the nested headless compositor, `show_seconds=false`, schedstat CPU): all four stats timers removed 2 ms vs 9-11 ms per 60 s (0.003% vs 0.016% of one 3 GHz core); wakeups/s 0.62 vs 1.12 (1 rep). Aligning the timers can only recover a fraction of this upper bound, which is below anything visible; not implemented. |
