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
6. Pointer: busy ring does not spin (Windows 7's does); no Xcursor theme is generated for other programs (GTK/Qt apps still show the
   installed cursor theme); the hand shape is an approximation of Windows 7's.
7. Mouse: nothing is applied for non-libinput pointers (VMs with absolute devices, nested); no scroll speed, double-click speed or
   button swap; the speed/precision were not checked on a real mouse.
8. Volume icon: mute is not read (VolumeSource gives percent and availability only), so a muted sink shows its level.
9. Network: mobile data (ww* interfaces, NetworkManager modems) was only tested with fake sysfs trees, never a real modem; the signal
   for mobile is not read (all bars when up); NM/wpa back ends are untrained and untested here.
10. Window frame: `frame_px` has no Settings control; 1024x768 was not run exactly (the headless output is fixed at 1280x720);
    resizing through the frame was driven only with the virtual pointer, never a real mouse; no rounded outer corners.
11. Training: window and taskbar clicks assume 1280x720 with the taskbar at the bottom or top; the tray, NetworkManager, wpa_supplicant
    and the PAM locker are untrained.
12. Older items still open from 2026-10-06 (state note `fleetwm-state-2026-10-06b`): direct scan-out check with `vkcube`; pressed
    state of caption buttons not passed by the compositor; glass for Tiling borders, tooltips and Settings windows; real NM/wpa connects;
    install.sh silent hang analysis; PGO stage 2/3 prewarm; multi-monitor design (afternoon prep note in the bank).

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
