# What is done, what is left (state at 0.4.0, 2026-10-07)

Kept in the repo so any machine that clones it knows where things stand. The full story with numbers: `CHANGELOG.md`,
`docs/OPTIMIZATIONS.md`, `docs/PERF_LOG_2026-10-07.md` (every step of the performance round), `docs/PERFORMANCE_FINDINGS.md`.
The same list lives in the memory bank (`projects/fleetwm-state-2026-10-07-v0.4.0.md`).

## Done in 0.4.0 (v0.4.0, GitHub release published)
- Performance round on the Celeron laptop: hardware facts and memory bandwidth, governor comparison (no difference), idle baseline per
  thread, idle wakeup trace, compositor profiles (idle: flat; scrolling terminal: 53% is Mesa's shm upload memcpy, near the memory limit).
- Icons: battery (plug on AC, charge sweep, green full, red under 10%), Windows 7 network icons (Wi-Fi bars, wired, mobile data),
  speaker volume icon (percentage in the tooltip), tachometer for the power mode; all drawn once per state and cached (bar CPU -11..13%).
- Taskbar order: tray, keyboard layout, volume, network, power mode, battery, clock last; no power button on the desktop taskbar.
- Window frame (Desktop layout): sides and bottom, glass or flat, `frame_px` in theme.toml; the titlebar is borderless and full width.
- Pointer: Windows 7 Aero (glass, with shadow) / flat, 14 shapes, cached, drawn in code. Mouse: Settings -> Mouse, speed 1-11 and
  Enhance pointer precision through libinput.
- PGO training: 85 s instead of 150 s, functions run 33.5% -> 40.1%, compositor 46.7% -> 60.5%, bar 67.6% -> 80.7%; found and fixed
  shortcuts that never fired (wtype modifiers) and unclean exit on SIGTERM with PipeWire running.
- Glass against flat measured: CPU and GPU identical, nothing left to remove.

## Left to do
1. IPC buffer test (Unix socket SO_SNDBUF/SO_RCVBUF, pipe sizes) for bulk transfers (PERFORMANCE_FINDINGS 2.5): not started.
2. Pointer-motion repaint log (2.2) for the bar, start menu and titlebar buttons: not started.
3. Bar idle wakeups: five unaligned timers (clock 1 s, stats 2 s, disk 5 s, network 5 s, battery 15 s) and a new timerfd per clock tick;
   align them to the clock tick and reuse one timerfd. Not measured as worth it (idle bar 0.33%, compositor 0.25%).
4. Re-measure the PGO build against a non-PGO build on the laptop (PGO speed was never re-measured; Lestrix found PGO no gain).
5. Final summary table (before/after per item) at the top of `PERF_LOG_2026-10-07.md` and in `OPTIMIZATIONS.md`.
6. Pointer: the busy ring now spins (12 frames at 12/s, only while shown: +15 ms CPU per 10 s of the compositor on the VM, none otherwise); no Xcursor theme is generated for other programs (GTK/Qt apps still show the
   installed cursor theme); the hand shape is an approximation of Windows 7's.
7. Mouse: primary button (left/right) and natural scrolling are in Settings -> Mouse and applied through libinput (compiled and the page checked at 1024x768; the libinput calls were never run, the VM has no libinput pointer). Still open: nothing is applied for non-libinput pointers (VMs with absolute devices, nested, virtual); no scroll speed or double-click speed (libinput has no scroll speed, double-click is up to the clients); speed, precision, buttons and scrolling were not checked on a real mouse or touchpad.
8. Volume icon: mute is read (PipeWire Props and wpctl "[MUTED]"): red slash and "(muted)" in the tooltip; checked rendering only, the VM dummy sink cannot be muted.
9. Network: mobile data (ww* interfaces, NetworkManager modems) was only tested with fake sysfs trees, never a real modem; the signal
   for mobile is not read (all bars when up); NM/wpa back ends are untrained and untested here.
10. Window frame: `frame_px` is now a Settings -> Theme spin ("Window frame (px)"); exact 1024x768 now runs (headless outputs take custom modes; bar and metric tooltips checked there, 2026-10-08);
    resizing through the frame was driven only with the virtual pointer, never a real mouse; outer corners are rounded now (radius 7, top corners of the titlebar and the bottom strip; not for maximized or snapped windows; the content corners stay square like Windows 7).
11. Training: window and taskbar clicks assume 1280x720 with the taskbar at the bottom or top; the tray, NetworkManager, wpa_supplicant
    and the PAM locker are untrained.
12. Older items still open from 2026-10-06 (state note `fleetwm-state-2026-10-06b`): direct scan-out check with `vkcube`; pressed
    state of caption buttons (done 2026-10-08: drawn while held, acts on release over the same button, cancels when released elsewhere); glass for tooltips, Fleetwm's own windows (Settings, Shortcuts, language picker: the compositor puts the frosted wallpaper behind them, `compositor/glass_backdrop.*`, and they paint a see-through background) and the Tiling borders (translucent, not looked at on screen) done 2026-10-08; real NM/wpa connects (the wpa back end is now trained against `scripts/pgo-fake-wpa.py`, a pretend supplicant: page open + scan polling, checked at 1024x768; NM still untrained, the VM has no NetworkManager);
    install.sh silent hang (hardened 2026-10-08, see below); PGO prewarm (built 2026-10-08: `src/common/prewarm.*`, findings section 14; gain is below the noise on the VM, laptop numbers open; stage 3, shrinking the resident size after start-up, not built); multi-monitor (`docs/MULTI_MONITOR.md`: stages 1-4 built 2026-10-08 and run on two headless outputs; stage 5 tiling per screen, stage 6 Settings, and every real-hardware check still open).

## Left to find out
- What closed the owner's live `foot` terminal (pid 792) on the laptop around 20:36 during the round (alive at 19:45; every kill was by
  recorded PID; unexplained).
- Whether the 0.4.0 PGO install on the laptop is faster or slower than the 0.3.0 one (needs the owner's install there).
- Whether acceleration and speed settings behave on the laptop's touchpad and a USB mouse (libinput path).

## Machines and state to know about
- Laptop (192.168.0.182): `kernel.perf_event_paranoid` is 1 (runtime), governor schedutil, `~/perf-test` and `/tmp/gb` hold test builds
  and tools; the owner reinstalls by fresh git clone.
- VM `fleetwm-dev` (192.168.0.223): clean Debian 13, pixman, started by the owner; `~/perf-test/{C,D}` and `nest_headless.sh`.
- Build rig: `perflog/2026-10-07/rig/README.md` (Docker image, the compositor only builds there, not on the dev PC).

## Performance test list from the Lestrix round (worked through 2026-10-08)
Results per row: `docs/PERFORMANCE_FINDINGS.md` section 9, raw files in `perflog/2026-10-08/`. Done: 13 (perf on the VM; needed `perf_event_paranoid` 3 -> 1, runtime only, approved), 6 and 10 (kept: IPC line parsing with a head offset, 4096-byte recv buffers), 7, 4, 2, 3, 5, 8, 9 (rejected or not applicable, with numbers). Still open: row 1 (render CPU/GPU per frame on a real GPU, needs the laptop or Lestrix GPU numbers), row 11 (key-to-pixel latency, needs a GPU and a camera or timestamps), row 12 (Lestrix under Fleetwm: the VM lacks SDL2, so it needs `apt install` there or the laptop), row 3 on the Celeron (tested only on the VM i5-8500), row 14 (no UI changed this round). The laptop (192.168.0.182) was unreachable on 2026-10-08; the VM reset `perf_event_paranoid` to 3.
 Idle wake-ups per process measured 2026-10-08 (findings section 11): nothing to change; the Celeron table is still open.\n
## Added 2026-10-08 (owner request): metric tooltips
CPU, memory, GPU and disk tooltips show the detailed readings (see CHANGELOG, Unreleased). Verified on the VM at 1280x720 and 1024x768 (CPU per core, scheduler latency, memory breakdown, live disk write speed). Not verifiable there, needs hardware: battery/RAPL power draw, per-core clocks, AMD sysfs and NVIDIA readings (parsers are unit-tested against fake trees). "CPU latency" was read as scheduler wake-up latency (a 200 us sleep, best of 5); say if another latency was meant.

## install.sh hang hardening, 2026-10-08
The apt phase could wait without a word. Every apt call now runs with `NEEDRESTART_MODE=a NEEDRESTART_SUSPEND=1`, `DPkg::Lock::Timeout=300`,
http/https timeouts of 30 s with 3 retries, and stdin from /dev/null (`apt_update` and `apt_install` in `scripts/install-stats.sh`). The
progress helper `ui_live` now reports a step whose output stops growing for 120 s (`UI_STALL_SECS`), every 120 s, with the apt/dpkg/needrestart
processes that could be holding it. Checked: syntax, the stall note against a fake 6 s step, apt accepting the options (`apt-get -s`). Not
checked: a real fresh install on a machine where needrestart actually prompts (no such machine here); the cause of the original hang is
still a suspect (needrestart), not a proven one.

## Cached RAM (htop yellow) on the real machine
`scripts/ram-report.sh` (read-only, no root needed, more with sudo) splits "Cached" into reclaimable cache, tmpfs/shared memory that is stuck, and mapped
files, lists the programs that hold the most file-backed memory, the biggest files in the page cache (`fincore`), tmpfs users, slab and the memory sysctls, and
ends with a short list of experiments (one needs the owner's OK: dropping caches). Run it on the laptop when it is idle, once right after boot and once after the
cache has built up, and keep both outputs. Tried on the dev PC: it ran in 10 s and found the answer there (browsers' and editors' binaries, apt .deb files, /tmp as tmpfs).

## Celeron laptop round, 2026-10-08 evening (findings section 15)
Done: idle wake-ups (compositor 2.4-2.6/s, bar 1.8-4.3/s), cold start (+108 ms evicted), prewarm A/B (replay no gain, parent-early +66 vs +107, full file 0) and end to end (prewarm ON 31 ms slower), LTO / no-LTO / `-march=native` A/B (noise), Intel GPU and per-core clock sysfs readable, RAPL not readable as a user, boot RAM report.
Still open: prewarm design for slow disks (parent asks early + whole files; keep off by default until it wins end to end), why the bar wakes 2-4/s on the Celeron against 1.2/s on the VM, lock applet exiting by itself in the nested rig, RAM report after the cache has built up, power line without RAPL, libinput, mobile data, NM, hotplug, GPU render numbers (owner).

Added later 2026-10-08: prewarm is now asynchronous (the 36-44 ms regression is gone, no gain either: findings sec 16; the cold cost is in the compositor's own start, whose prewarm would have to come from the greeter, root-run, owner decides); GPU render cost measured on the laptop (sec 17: gles2 costs 9x the compositor CPU of pixman while a shm client scrolls, 73% of it a memcpy in Mesa; GPU busy 22%). Still open from the GPU list: key-to-pixel latency (row 11), Lestrix under Fleetwm (row 12, dmabuf client avoids the copy).

## End of 2026-10-08 (kept in sync with the memory bank note `projects/fleetwm-state-2026-10-08-end-of-day.md`)
Done since the Celeron round: Windows 7 frame defaults from real screenshots (`docs/WINDOWS7_FRAME_REFERENCE.md`), glass on by default with opaque window bodies, `--version`/`--help` with the author credit on every program, a damaged config file keeps the defaults, children start with a clean signal mask (Ctrl+C and git work in terminals opened from the start menu), the default terminal keeps its arguments (Lestrix Lite), install time on the VM recorded (findings section 19).
Open: greeter-side prewarm (needs a privilege-dropped read; owner decides), application icon in the titlebar, Aero blue glass colour, `STATS_BUILD_CPU` is 0 in the install summary (scripts/install-stats.sh, "Parallel speedup 0.00x"), key-to-pixel latency, Lestrix under Fleetwm, libinput on a real mouse, mobile data, NetworkManager, real monitor hotplug, multi-monitor stages 5-6, a 0.5.0 release (version bump, changelog, tag, GitHub release).

## File manager and desktop (2026-10-09, owner request: `fleetwm-fm`, `fleetwm-desktop`)
Built on the VM, not committed. What it is: `docs/FILE_MANAGER.md`, `docs/DESKTOP.md`; what was measured: `docs/FILE_MANAGER_PERFORMANCE.md` (raw files in `perflog/2026-10-09/`).
1,007 tests pass (260 file manager and desktop tests); AddressSanitizer + UndefinedBehaviorSanitizer and ThreadSanitizer (94 window and transfer tests) were repeated on this final code and are clean. To test on the VM: `scripts/build-pgo-auto.sh` (the training now runs
`fleetwm-fm --train` and a desktop menu phase), then install, then look at both layouts.
Open, most important first:
1. Network places were never run against a real Samba, SFTP, FTP or Nextcloud server; eject and mounting never on a real USB stick.
2. Clipboard and drag and drop with a second Wayland program were never run (only the unit tests with a fake host).
3. Native FTP/SFTP/WebDAV/SMB clients: a rewrite of the data path (see FILE_MANAGER.md). Not started.
4. Group by in the icon views, "Other application..." under Open with, a Settings page for `desktop.toml` (the shortcut card can only be turned off in the file).
5. Performance next steps with an estimate: prefix-key array for the 100,000-name sort (about 20 ms), overlap read-back with writing for big files.
6. The pointer was never moved by a real mouse in a running session; everything was driven by tests and the headless compositor.

## Taskbar options (owner request 2026-10-09, not started)
One "Taskbar" page in Settings (the Bar page grows into it; Date & Time keeps only the system clock and NTP, the clock format and bar time zone move here):
1. Order of the elements (start, workspaces, pinned apps, window list, tray, keyboard layout, volume, network, power mode, battery, metrics, clock) as a list the user reorders (up/down, later drag), saved in `bar.toml` as `taskbar_order`; unknown names are dropped and missing ones appended, so an older file still loads.
2. A toggle per element (`taskbar_hidden = [...]`).
3. Auto-hide (`taskbar_autohide`): the compositor must stop reserving the strip and the bar must slide in from the edge on pointer contact; needs a compositor change (exclusive zone 0 plus a 1 px trigger strip), the largest part.
4. Start menu centred, Windows 11 style (`start_centered`): start button, pinned apps and window buttons form one centred group; the status area stays at the right edge.
5. Pinned apps (`pinned = ["firefox.desktop", ...]`): icon-only buttons that start the app, or focus its window when one is open; add and remove from the Settings page and from the window button menu.
6. Window buttons as icon and title or icon only (`taskbar_labels`).
Both orientations (horizontal and the vertical Left/Right taskbar) must follow the same order. Tests at 1024x768. The bar's two draw paths (`draw_taskbar_horizontal`, `draw_taskbar_vertical` in src/bar/main.cpp) are fixed layouts today, so step 1 is to turn them into one list of element widths that either path lays out.

## Task manager app (owner request 2026-10-09, not started)
`fleetwm-taskmgr`: code-drawn like the other apps (fleetkit, no GTK), Windows 7 Task Manager layout with the Windows 10/11 details, kept simple.
- Tabs: Processes (name, PID, CPU, memory, disk, user; end task), Performance (CPU graph with one small graph per core, memory graph and history, disk, network, GPU, and the numbers the bar already shows: CPU, RAM, GPU, disk, plus scheduler latency and audio latency), Startup/Services only if they stay small.
- The bar's metrics (CPU, RAM, GPU, disk) become off by default; right click on the taskbar shows "Task Manager" and opens the app, and the same values come from one shared reader (`src/common/hw_stats.*` today) so the bar and the app never disagree.
- Cost: it must not disturb what it measures. Sample only while the window is visible (stop on minimise/hidden), 1 s default tick, read `/proc` with one open file per source and `pread`, no per-process work on the Performance tab, redraw only damage, no allocation in the tick; measure its own CPU and RSS with the app idle and open (budget: under 1% CPU, a few MB) and write the numbers down.
- Reference: research with the browser/websearch skills (Windows 7 Task Manager, Windows 10/11 Performance tab, btop/htop, GNOME System Monitor, KDE System Monitor) before drawing anything; keep the reference screenshots out of the repo.
- Tests at 1024x768; unit tests for the /proc parsers with fixture files.
