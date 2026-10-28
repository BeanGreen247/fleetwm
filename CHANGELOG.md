# Changelog

All notable changes to Fleetwm. Versions follow `meson.build`; the dated tags are on GitHub.

## Unreleased

- Fixed the taskbar showing no icon for Wine programs and games that set their own (`_NET_WM_ICON`): the property name was looked up with a wrong length, so no program icon was ever read. An icon kept on another top-level window of the same program (Wine does this) is found too.
- The Task Manager's hover details open beside the hovered row instead of over the list below it, and show that GPU's details.
- The installers give Heroic Games Launcher's user desktop entry `--ozone-platform=x11` (`scripts/heroic-x11.sh`): started from a Wayland session it runs as a native Wayland client, where its game library stays blank and the window stops repainting after a resize.
- `fleetver` (also `winver`, and a Version details button in Settings -> About) shows the exact version, git revision, build time, system, license and project links.
- Right-click Kill app on a running taskbar entry ends the program and its children.
- Alt + left drag moves and Alt + right drag resizes any window (the corner nearest the pointer moves), for programs that draw no frame of their own such as Wine launchers.
- Fixed a crash at logout: the compositor asserted in wlroots (`manager_handle_display_destroy`) because the xdg-toplevel-icon listener was left attached; it dumped core on every logout since the window-icon change.
- Launchers are not games: Battle.net, Epic, GOG, Heroic, Lutris, a game's own launcher or configuration window (by title or the program's command line, `src/common/launcher_match.hpp`) no longer start GameMode, TuxThrottle or the fast desktop even though Wine and Lutris give them a Steam-style class, and a launcher maximized by its own button no longer turns into a fullscreen window that hides the taskbar.
- Alt+Tab and the other shortcuts are only withheld from a fullscreen window: a Wine launcher or Electron app that grabbed the keyboard used to make Alt+Tab do nothing.
- The installers (`install.sh`, `install-fedora.sh`, `install-alpine.sh`) set the GTK 3 and GTK 4 cursor theme to Fleetwm's own (`scripts/set-cursor-theme.sh`: `gtk-cursor-theme-name=fleetwm-aero`, size 32; the original `settings.ini` is kept once as `settings.ini.fleetwm-bak`), so the pointer is the same in every window out of the box.
- One pointer in every window: the compositor writes its Windows 7 style pointer as an xcursor theme (`fleetwm-aero` with glass, `fleetwm-flat` without, in `~/.local/share/icons`, rewritten only when the version changes) and sets `XCURSOR_THEME` / `XCURSOR_SIZE` to it, so programs that load their own cursor image (Chromium and Electron, Steam, Wine, Xwayland, Qt) no longer show a different cursor theme from the windows that ask the compositor for a named shape. The user's theme stays the fallback for names we do not draw. GTK 3 programs still read `gtk-cursor-theme-name` from `~/.config/gtk-3.0/settings.ini` (set it to `fleetwm-aero` for them).
- Every installer now installs the profile-guided build and nothing else: `install-fedora.sh` no longer has `FLEETWM_PGO=0`, `install-alpine.sh` runs `scripts/build-pgo-auto.sh` (with `FLEETWM_MESON_EXTRA` for its `-Dxwayland=false` build) instead of a plain release build, and both install with `meson install --no-rebuild`. `install.sh` (Debian) and `fleetwm-update`, which calls it, already did.
- Game support: pointer lock and relative motion (`pointer-constraints`, `relative-pointer`), keyboard shortcut inhibit (Super+Pause gives the shortcuts back), `presentation-time`, `fractional-scale`, explicit sync, variable refresh rate while a window is fullscreen, and aspect-correct fullscreen scaling for old games (`game.toml`: fit, integer, stretch, off). `docs/GAMING.md`; `scripts/gaming-tune.sh` is an opt-in, runtime-only tuning script. Built and unit-tested; not yet run with real games.
- GameMode for every game (Steam/Proton, Wine, Lutris, gamescope, listed apps, fullscreen non-video windows), `fleetwm-game` (GameMode, libstrangle frame cap, dedicated GPU in one launch prefix), the integrated GPU draws the desktop and the dedicated one is used on request (`desktop_gpu`, `--gpu discrete`, `PrefersNonDefaultGPU`), Settings -> Gaming and Settings -> Startup Apps (XDG autostart list). `docs/GAMING.md`.
- TuxThrottle integration: its Game Mode is switched on with the first game and off after the last (`tuxthrottle` in `game.toml`), and Settings -> Gaming shows its readout (profile, temperatures, dedicated GPU state). No effect without `tuxthrottlectl`.
- More game options: late render start, real-time priority through rtkit, switching the monitor to a game's own resolution, `wp_content_type` (all off by default except the hint), TuxThrottle lines in the bar's CPU/GPU tooltips, a taskbar colours section in Settings -> Taskbar, `install-fedora.sh` and `scripts/setup-hotspot.sh`.
- File manager start: no 25 ms hash benchmark on CPUs with SHA instructions. Task Manager and bar: a sleeping dedicated NVIDIA GPU is no longer woken by `nvidia-smi` queries.
- `install-fedora.sh`: profile-guided build by default (`FLEETWM_PGO=0` skips it; 5 min on a Ryzen 7 5800H), lock screen PAM service, opt-in login screen (`FLEETWM_GREETER`). Headless and nested compositors no longer trigger GameMode, TuxThrottle or real-time scheduling on the host.
- Gaming laptops: with `desktop_gpu = "integrated"` a dedicated GPU that has no screen connected is no longer opened by the compositor (it kept the card awake at 17 W and cost about 60 MB of compositor memory). Task Manager shows every GPU: a percentage per GPU in the sidebar, a graph and block each, all names in the header.
- A program that hides its window but keeps running (Steam going to the tray, an X11 window unmapping) no longer leaves its title bar and frame on the screen: the window's frame is hidden on unmap and shown again on map. Found on the gaming laptop, where Steam's hidden windows showed as empty title bars over the desktop; checked with an X11 test client in `scripts/game-input-test.sh`.
- Taskbar: system tray icons are 22 px by default instead of 16 (`tray_icon_size` in `bar.toml`, Settings -> Taskbar -> System tray); colours for the remote desktop pill and the keyboard layout pill; with several GPUs the taskbar shows each one apart ("AMD 3%", "NVIDIA 0%") instead of one combined figure; the power-mode gauge follows the power profile that is really active (power-profiles-daemon, live), so TuxThrottle and GameMode changes show up. 
- `fast_desktop` in `game.toml` (off by default): drops glass and blur while a game runs or the profile is Performance.
- The bar has its own XEmbed system tray, so Wine and Proton tray icons (the Rockstar launcher) show up and take clicks; it only connects once Xwayland is running (never starts it). Needs libxcb-composite at build time.
- Game boost: it now ends six seconds after the last game window closes, not at once, so a launcher closing as the game opens (Mount and Blade's launcher, Rockstar's) no longer switches TuxThrottle's Game Mode (fans, governor, GPU clocks) on and off three times in eight seconds. The power profile in force before the first game is remembered and put back last, after TuxThrottle's off scripts (which always set "balanced") and after falcond's own restore; a "performance" seen at that moment is taken for a game tool being quicker and not remembered. The decisions are logged to the session log.
- Taskbar glass: the light ray over the taskbar is weaker (30 % by default, `glass_ray_percent` in bar.toml, Settings -> Taskbar -> Glass), it looked like a white shimmer, most of all on a large screen. The start orb takes a third of its look from the detected colour (accent or the start colour in Settings), its four panes are larger with a dark shadow and a light edge, and the gloss no longer washes them out. The orb is painted once and copied (0.47 ms to 2 microseconds per repaint).
- Settings -> Gaming: a list of the installed Steam games with TuxThrottle's per-game MangoHud and dedicated-GPU switches. TuxThrottle's `mangohud` gate ignores a `mangohud %command%` launch option unless the game's switch is on, which is why Steam seemed to ignore the MangoHud settings. A headless or nested bar no longer changes the real machine's power profile.
- Settings -> Gaming: the TuxThrottle readout (profile, temperatures, Game Mode on/off) refreshes by itself every two seconds while the page is open; the Refresh button is gone. `scripts/settings-tux-refresh-test.sh` checks it.
- Tray: items without Activate / ContextMenu but with a dbusmenu `Menu` (Steam and everything built on libappindicator) now show that menu when clicked, with either button; submenus are flattened ("Friends > Online"), disabled and hidden entries left out. Before, neither click did anything.
- X11 tray icons keep their transparent background (ARGB tray visual).
- File manager and desktop: opening a `.desktop` shortcut (applications, Lutris and Steam shortcuts, hand made ones), an executable program (ELF, AppImage), a script with a `#!` line (in a terminal) or a Windows `.lnk` (through Wine) now runs it instead of opening the default application; symbolic links to them work too. Properties: the checksums are shown whole (broken into lines of 32 characters) and the dialog is wider.
- Compositor: switching variable refresh rate on or off is now part of the frame's own commit; a separate commit made the frame's commit fail with "a page-flip is already pending" at the moment a game went fullscreen.
- Borderless X11 games as big as the screen (Wine, Proton) count as fullscreen, so the taskbar no longer shows over them. Task Manager lists each GPU as its own entry (GPU 0, GPU 1). The compositor serves KWin's `org.kde.KWin.InputDevice` interface so TuxThrottle's touchpad switches work and are kept across Settings -> Mouse changes.
- Alt+Tab keeps the recent-window order: stepping through several windows no longer reverses the ones in between, so the next Alt+Tab returns to the window you came from.
- The bar sets the saved power mode with a direct D-Bus call instead of spawning `powerprofilesctl` (a Python script): session start uses 47% fewer CPU cycles in the headless rig (593.6 M to 314.2 M), measured with perf and Hotspot, `docs/HOTSPOT_PROFILING.md`.
- Taskbar colours: `[taskbar_colors]` in `bar.toml` (background, start button, active window button and running lines, clock, status icons), empty entries follow the theme.
- Builds and runs on wlroots 0.20 (Fedora, Nobara): the xdg-decoration mode is sent at the toplevel's initial commit, `wlr_xdg_surface_get_geometry` is guarded; `scripts/install-deps-fedora.sh` installs the build dependencies, `scripts/fix-keyring-pam.sh` refreshes a stale login-screen PAM file that left the keyring locked.
- Glass costs 27-31% less compositor time to rebuild: the box blur (glass backdrops, window shadows, caption glow, wallpaper blur) is 2.9x faster with byte-identical output (a 16-bit multiply-high instead of a 32-bit multiply per lane, and an SSE2 4x4 pixel transpose): 212 to 72 us for a 160x85 backdrop, 2055 to 697 us at 480x270; in the compositor with a glass window over three repainting terminals glass time went 3.4 to 2.4 ms/s idle, 4.9 to 3.6 while moving, 4.5 to 3.1 while resizing (dev VM, pixman).
- The glass code gives back its read-back buffer (up to 6 MB for a large window) and the blur its working copies 4 s after the last glass rebuild instead of keeping them for ever.

- Tried and rejected: CET off, `-fno-plt`, no semantic interposition, no async unwind tables, no math errno/trapping math, `noseparate-code`, `norelro` as one set: compositor frame cost, binary size and PSS identical within noise (`docs/OPTIMIZATIONS.md`, 2026-10-27).

- Less resident memory in the compositor: ten seconds after start it drops the page-table entries of library code that start-up ran once (Mesa, LLVM, wlroots), 1.2 MB less PSS and 5 MB less RSS on the VM with pixman; the real-GPU effect is not measured yet. With a zram swap active it also sends the wallpaper buffer to zram (flat and smooth pictures compress to almost nothing). New `src/common/page_release.hpp`, tests in `tests/test_page_release.cpp`.
- The login service pages itself out while a session runs (it has to stay for PAM): with zram a 16 MB heap went from 19.7 to 3.3 MB resident in a test program; the real greeter is unmeasured.
- Binaries built with `-ffunction-sections -fdata-sections -Wl,--gc-sections`: compositor 15% smaller stripped (1211960 to 1027000 bytes); resident memory unchanged within noise.
- `install.sh` caps the systemd journal (`SystemMaxUse=100M`, `RuntimeMaxUse=16M`) in `/etc/systemd/journald.conf.d/50-fleetwm.conf`; journald was 46.5 MB on the dev VM. Effect unmeasured.

## 0.5.0 - 2026-10-26

- `install.sh` installs ufw and allows VNC (5900 and 5901) and RDP (3389) on TCP and UDP, plus SSH (22/tcp) so enabling the firewall cannot lock you out. It does not switch the firewall on or off; `FLEETWM_NO_FIREWALL=1` skips the step. RDP and VNC connections that fail now log why (client address, offered security, TLS error, rejected user name) to the session log.
- The login screen unlocks the login keyring (gnome-keyring) with the password you type, so ssh keys, Chromium, git credentials and anything else that keeps secrets in it find it open after login instead of asking: `pam_gnome_keyring.so` is in the greeter's PAM service (auth, session with `auto_start`, password), and `install.sh` installs `gnome-keyring` and `libpam-gnome-keyring`. `docs/GREETER.md`, "Keyring".
- Settings redesigned as one place for everything, from Windows 11 Settings, macOS System Settings and the classic Control Panel: a grouped sidebar with icons and a profile card, **a scrollbar on the sidebar** (it was cut off after "File Manager" with no way to see the rest), a header with back/forward, a breadcrumb and a "Find a setting" search (settings by name and keyword), a Home page with recent pages and a Control-Panel style category view (View by Categories, Large icons or Small icons, with task links), an icon, name and description at the top of every page, and a new Account page for the name and picture. `fleetwm-settings --search TEXT`. New `scripts/headless-shot.sh` takes screenshots in a private headless compositor. `docs/SETTINGS.md`.
- Remote desktop, VNC picture coding and controls: Tight encoding with JPEG (libjpeg-turbo, optimized tables) next to ZRLE: text and flat colour stay lossless and 2x smaller, photographs, gradients and video 3-100x smaller and up to 15x faster to encode (1280x800, `perflog/2026-10-26/`); decoded correctly by libvncclient 0.9.15, the decoder Apache Guacamole uses. A dense UI screenshot is not smaller than ZRLE. New Frame rate dropdown (5-60, default 30) per protocol: before, the Quality slider set the update interval (quality 30-49 meant 5 updates a second, the cause of the lag on a LAN). The bandwidth limit has a floor computed from frame rate, quality and size (shown in Settings, slider skips lower steps, the server raises a lower limit). The screen is copied no faster than the fastest viewer's frame rate. Keep this password / Set your own password (for saved Guacamole connections). RDP gets the frame rate, floor and capture pacing; its codec is unchanged. Design and measurements: `docs/REMOTE_DESKTOP_INTERNALS.md`.
- Login screen session picker: `fleetwm-greet` lists and starts every desktop in `wayland-sessions/` and `xsessions/` (MATE, Xfce, KDE, sway, ...), not only Fleetwm; X11 sessions through `xinit` (the installer adds `xinit` and `xauth`); the last session is remembered. The installer disables an enabled display manager for the next boot (never stops it, so nobody is logged out; `FLEETWM_KEEP_DM=1` opts out). The greeter unit backs off when another display manager holds the screen. Untested on real hardware. `docs/GREETER.md`.
- Low-memory build: `FLEETWM_PIXMAN_ONLY=1 ./install-alpine.sh` builds wlroots with the software renderer only (no Mesa, EGL, GBM or LLVM, no XWayland). Compositor on Alpine, headless, 1280x720: 53.8 MB to 12.3 MB PSS; compositor + bar + wallpaper + desktop 65.3 to 23.6 MB. The compositor compiles against either wlroots (`gles2_compat.hpp`); EGL and GLES2 are optional in `meson.build`. Real KMS output on this build is untested.
- Alpine Linux support (experimental, boot untested): `install-alpine.sh` (apk packages, build, install, OpenRC services, login screen on tty1 through inittab with a `pam_elogind` PAM service); the code builds on musl (missing `<sys/types.h>`, `renameat2`/`RENAME_NOREPLACE` through the syscall, `mallopt`/`malloc_trim` only on glibc) and all 1234 unit tests pass in an `alpine:3.21` container. `docs/ALPINE.md`. Measured there: bar, desktop and wallpaper use 9-34% less private memory than on glibc; the compositor maps 33 MB of libLLVM through Alpine's Mesa packaging (see `docs/OPTIMIZATIONS.md`).
- Less memory in the programs that never hash a file: libcrypto is opened on the first checksum instead of being linked, so the desktop process drops 3621 to 3023 KB PSS (-17%, one per screen), and Settings and others lose the same start-up cost; `-Wl,--as-needed` project-wide; helpers cap malloc arenas at 2.
- Autostart dedupe fixed: a killed compositor no longer leaves a second lock applet (0.7 MB each) behind for the next one; the check reads `/proc/<pid>/exe` instead of forking `pgrep` per helper. The resident greeter trims its heap once the login compositor is gone (unmeasured).
- VM graphics: on a virtual or basic GPU without 3D (virtio-gpu without virgl, Standard VGA, QXL, Cirrus, VirtualBox, Hyper-V, framebuffer) the compositor now uses the pixman renderer even when a render node exists. Before, the greeter's `WLR_RENDERER_ALLOW_SOFTWARE=1` let wlroots run Mesa's llvmpipe there: measured on a Proxmox VirtIO-GPU VM (1 terminal scrolling 10 lines a second, 20 s): compositor 146 MB and 8.4% of a core, now 21.4 MB and 0.4% (-125 MB, 21x less CPU); the system's memory in use fell from 510 to 453 MB. VirGL (3D) and real GPUs keep GLES2. An explicit `WLR_RENDERER` still wins.
- Remote desktop: render resolution and colour depth dropdowns per protocol (the picture is shrunk with a box filter, proportions kept, never enlarged; VNC 8/16/24-bit, RDP 16/32-bit; "Automatic" follows the quality slider). Measured on the dev VM, VNC on a scrolling-text workload, KB/s: full size 24-bit 1722, 8-bit 829, 800x450 1312, 800x450 8-bit 453, 640x360 8-bit 292 (-83%). The mouse is mapped back to the screen; RDP reconnects when these change, VNC follows through DesktopSize.
- Bar: while a viewer is connected a pill shows the protocol (VNC, RDP or VNC + RDP, in the floating bar and as the new "Remote desktop" taskbar element); hover shows, per protocol, viewers, data sent and received, current up and down speed, CPU and memory of the viewers. `fleetwm-remote` writes the numbers every 10 s and at each change (the bar watches the file, nothing is polled). Settings -> Remote Desktop shows the same plus a tally kept in `~/.local/state/fleetwm/remote-usage.txt`: today, and the average per day over 7 and 28 days (sent, received, connected time, CPU) and the all-time total.
- fleetwm-remote uses 18% less memory with two viewers (26.4 to 21.7 MB at 1280x720: tile and encoder buffers given back, malloc thresholds pinned). New `scripts/ram-budget.sh` (memory of Fleetwm against a budget), `scripts/gpu-report.sh` (graphics hardware, real or virtual, and the renderer Fleetwm will use); the compositor logs `fleetwm: renderer ...; display devices: ...` at start. `docs/HARDWARE.md` has the analysis for Proxmox virtual GPUs and the E490 history; `docs/OPTIMIZATIONS.md` the memory budget (the desktop is 43.5 MB; the whole system cannot be under 200 MB on the dev VM).
- Remote desktop for slow links and less CPU: each server has a Quality slider (colour depth and update rate), a Bandwidth limit slider (32 KB/s to 8 MB/s, paced, with the colour depth dropping one step at a time when a viewer needs more than the limit, and the link's own speed learned from how long sends take) and an Encryption switch (VNC: VeNCrypt, tested with TigerVNC, off by default so every viewer works; RDP: TLS on by default, off gives standard RDP security without encryption, tested with FreeRDP `/sec:rdp`). Measured on the dev VM (1280x720 headless, a 1000x600 terminal printing a random full-width line ten times a second): VNC with CopyRect 157 KB/s and 5.3% of a core against 1734 KB/s and 14.2% without (the viewer's picture is pixel-identical to a fresh full picture after 71 CopyRects); RDP 16 bit run-length coded at quality 50 sent 35.9 MB in 14 s against 154.2 MB for 32 bit raw (4.3x less; random text is the worst case for run-length coding); a 256 KB/s limit gave 264 KB/s measured, 64 gave 76 (one update is never split); the idle daemon wakes 0 times in 30 s (the config file is watched with inotify instead of polled every second). Tiles that came out identical are no longer sent, and the ZRLE encoder allocates nothing per tile. Settings sliders can show their own value text (`Ui::slider(..., value_text)`).
- Remote desktop, built in: Settings -> Remote Desktop has a switch for a VNC server and one for an RDP server (`fleetwm-remote`, started with the session, no xrdp/x11vnc/wayvnc needed; named fleet-first so it cannot be mistaken for RustDesk). Switching a server on makes a new random password (VNC 8 characters, the protocol's limit; RDP 16) and starts it; switching it off closes it, disconnects everyone and forgets the password. The page shows the addresses, port, RDP user name (the login name), password, status and the number of viewers. VNC speaks RFB 3.8 with password login, Raw and ZRLE; RDP speaks TLS security (user name and password inside the TLS tunnel, no NLA) with 16 or 32 bit bitmaps, so Apache Guacamole (VNC, or RDP with security "tls" or "any" and "ignore certificate"), FreeRDP, Remmina, mstsc and any VNC viewer work. The screen is the first monitor, grabbed with wlr-screencopy only while someone is connected and only where it changed; mouse, wheel and keyboard (layout from Settings -> Keyboard) come back through the virtual pointer and keyboard protocols. Five failed logins in a minute lock logins for the rest of the minute. Tested on the dev VM against FreeRDP 3.15 (32 and 16 bit, typing with Shift and symbols, click, wrong password) and a scripted VNC client (Raw, ZRLE, wrong password, typing, click, toggling in Settings). Config: `remote.toml` (mode 0600). Docs: `docs/REMOTE_DESKTOP.md`.
- Glass costs much less to keep: a second pass over the glass path (callgrind of the compositor with three printing terminals under a glass window, then each hot spot fixed and re-measured) brings the compositor's CPU in that scene to idle 15-17 ticks (was 41), move 31 (was 67), resize 44-48 (was 71), against 8, 19 and 30 with glass off (2.0x, 1.6x, 1.5x instead of 5.9x, 2.8x, 2.2x). What changed: (1) a window being dragged or resized follows the pointer once per output frame instead of once per pointer event (about 125 a second, of which the 24 FPS cap showed a fifth): each position change of a glass window made the scene redo about eight source boxes at roughly 20,000 instructions each, 37% of the compositor's drag work; glass off gains too (move 24 to 19 ticks); (2) the blur is vectorisable now (running sums along rows with 16-bit lanes and a multiply-high division, the horizontal pass done on a transposed copy; still byte-identical): 3.6x faster than the old loop with a generic build and 6.6x with `-march=native`, which the install uses; (3) reading and shrinking the windows below is 3x cheaper (two lanes per word, no clamping inside the region, one reused read buffer; checked equal to the old averaging on 11,781 pixels); (4) the four frame parts have their own keys, so only the parts a changed window touches are rebuilt, and a rebuilt picture that is byte-identical to the old one is dropped (no redraw); (5) source boxes the scene already has are not set again. `scripts/grab-stats-run.sh` gained `COMP_WRAP`, `ONLYIDLE`/`ONLYMOVE` for profiling and GRABSTATS fields `glass_pieces`, `glass_same` and the time per stage.
- Glass frames over windows that keep repainting cost the compositor 24-41% less CPU: with three scrolling terminals under a dragged glass window (headless, pixman, 1280x720, 8 s phases, compositor CPU ticks) idle 41 to 23, window move 67 to 50, resize 71 to 54 (glass off: 7, 24, 32). Three changes: the box blur of the glass pictures (and of the caption glow, window shadow and wallpaper) is one shared routine, `kit::box_blur_u8`, 2-4 times faster and byte-identical to the four copies it replaces (unit test `BoxBlur`); a window below that only repainted rebuilds the glass at most every 120 ms instead of 45 ms (moves, resizes, restacks and focus changes still follow at 45 ms); and the glass pictures are no longer rebuilt on every step of a drag, only when the glass leaves the area they hold or something under it changed (the key `glass_backdrop_key` no longer contains the glass position). `scripts/grab-stats-run.sh --terminals N` runs this scene.
- Glass mode uses 3.6 MB less memory in the compositor at 1280x720 (about 8 MB at 1920x1080, about 32 MB at 4K): the frosted wallpaper behind the glass frames is now kept as the small blurred picture (480 px wide) and the scene scales the part it needs, instead of a copy painted out to the whole output size per output. Compositor anonymous memory in a headless session with Settings, Task Manager and the file manager open: 8028 kB to 4416 kB (glass off: 3160 kB). The source box stays half a pixel inside the picture so no renderer samples outside it; unit test `GlassBackdropMap` checks the pixels against the old route (at most 3 of 255 apart, four output sizes).
- Loading the installed applications (launcher, start menu, Settings, every program's first icon lookup) takes 1.55 ms instead of 4.7 ms for 52 entries (best of 40, 3 runs each, 89 files scanned): `src/fleetkit/desktop_entry.cpp` reads each file once and skips the translations it does not need instead of copying every key into a map; the result is byte-identical in the C, cs_CZ and de_DE locales. New tests `DesktopEntryParse.*`.
- `scripts/build-pgo.sh` takes `none` (same flags and LTO, no profile) and `FLEETWM_PGO_DIR`; `FLEETWM_PGO_AGGRESSIVE=1` adds atomic profile counters and one LTO partition. `scripts/pgo-train-session.sh` writes the CPU ticks of every program with `FLEETWM_PGO_CPU_REPORT=file`. Measured: no PGO variant is clearly faster than none (see `docs/OPTIMIZATIONS.md` "Round 2026-10-22, PGO").
- Bar: while the Metrics element is hidden (the default) the 2 s stats tick no longer reads `/proc/stat`, `/proc/meminfo` or the GPU counters, and no longer starts `nvidia-smi` (one process spawn every 6 s on NVIDIA machines); the disk timer skips `statvfs` too. A config reload refreshes them when Metrics is shown. File manager: the thumbnail thread starts with the first thumbnail request instead of with every window. From the Lestrix (on-demand threads, idle skips), Painite and CreeperCrest (no subprocess per poll when nobody looks) rounds; with Metrics hidden the bar made about 28% fewer context switches (1100 to 785 per 30 s under strace, headless Desktop layout, 2 runs each; the NVIDIA spawn is not measured, no NVIDIA machine here). The 2 s stats and 5 s disk timers are now removed while Metrics is hidden, not just skipped.
- Task Manager, Cache tab: the placeholder row "Fleetwm apps: requires instrumentation" is replaced by real counters of that window's own text-measure, caption-glow, titlebar-glass and caption-button caches (hit rate, lookups, entries, bytes, evictions; `kit::text_cache_stats()`, `TitlebarCacheStats::hits/misses/evictions`).
- Media applet waveform: the bars now follow the sound the speakers play (a PipeWire capture of the default output, 32 log-spaced bands from an FFT, `src/common/spectrum.*`, `src/bar/spectrum_source.*`), listening only while the waveform is shown and a player is playing; without PipeWire they keep the old clock-driven animation.
- Closing a GTK dialog (Caja's About window) could abort the compositor in `wlr_seat_keyboard_notify_enter` (null surface): `focus_view` ignores a view with no surface yet and `focus_next_after` skips such views.
- Brave, Chrome, Chromium, Edge and Vivaldi no longer show their own minimise/maximise/close buttons under a Fleetwm titlebar: in the Desktop layout the compositor sets `browser.custom_chrome_frame` to false in each profile (skipped while the browser runs, applied at the next login or layout change), and back to true in the other layouts.
- Text with characters Inter does not have (braille spinners, dingbats, box drawing, CJK, emoji: terminal window titles, track names) is drawn with a font that has them instead of an empty box. `kit::draw_text`/`measure_text` look the font up with fontconfig once per character (`fallback_face` in `src/fleetkit/fleetkit.cpp`, cairo-ft, still no Pango); Latin text keeps the old path.
- Power mode: `install.sh` installs and enables `power-profiles-daemon`, and the bar hands the saved mode to it with `powerprofilesctl set` at login and whenever it changes in Settings (before, only Settings called it once and nothing installed the daemon, so no profile was ever applied).
- Media applet: the title, artist and length now come through (the D-Bus type check in `src/bar/mpris.cpp` never matched on current systemd, so the applet showed only the player name, no time and no waveform); the previous and next arrows point the right way; the waveform moves smoothly (about 8 redraws a second while playing, resting low when paused) and also shows for streams with no length.
- Login on a machine that already has a display manager: `install.sh` leaves the Fleetwm login screen disabled when any display manager (lightdm, GDM, SDDM, greetd) is enabled or running or `/etc/X11/default-display-manager` exists, not only when `display-manager.service` is enabled (the greeter on tty1 and lightdm fought over the screen). When a display manager starts the Wayland session the compositor asks libseat for the logind seat (`LIBSEAT_BACKEND=logind`, unless already set) instead of the idle seatd seat, which had given it a seat that was not on screen (`gbm_bo_create failed`, nothing drawn). Recover a stuck session with `loginctl terminate-session <id>`.
- Taskbar desktop switcher: the numbers take black or white from the colour that really lies under them (the blurred wallpaper with the glass tint, then the highlight), so they read on any wallpaper, selected or not (`kit::readable_text_on`); Settings -> Taskbar has a "Virtual desktops" count (1-10, `taskbar_workspaces`) and with one desktop the switcher is hidden. Unfocused windows' caption buttons keep crisp edges and glyphs (only their glass fades), so they no longer look blurry over the frosted backdrop.
- Glass follows how Windows 7 really builds it (docs/WINDOWS7_FRAME_REFERENCE.md, sixth pass, from DWM's colorization parameters and an open-source Aero reimplementation, checked against measured screenshots): Sky `#74b8fc` tint at the measured strengths, a smaller blur that keeps the wallpaper's shapes visible, and the light ray as a screen-fixed reflection that shifts 10% with a window; and the glass now shows the other windows behind it, blurred (their frames and content), as Windows 7's does.
- No external TOML library any more: Fleetwm reads and writes its configuration with its own `fleettoml` (`src/common/fleettoml.*`, 800 lines, same `toml::` interface). A fresh clone no longer git clones toml++ (the old wrap is gone), and nothing has to be installed for it. Checked against toml++ 3.3 and 3.4 on about 200,000 mutated documents (same accept/reject answer, same values), under ASan/UBSan, and with the whole unit suite; parsing a typical config is 2.4-3x faster and writing 1.9-2.2x faster than toml++ (docs/OPTIMIZATIONS.md).
- The desktop looks like Windows 7 in both modes (docs/WINDOWS7_FRAME_REFERENCE.md, fifth pass): glass or opaque "Basic" window frames with the measured edge lines, tile taskbar buttons, a start orb, a show-desktop sliver (`SHOW_DESKTOP`), a white clock, icon-only buttons and no pin button by default. The "Theme colours" tint keeps the old flat look.
- Windows 7 light ray across glass titlebars and the taskbar; a softer, wider window shadow; Settings -> Taskbar "Line under each running program" (`taskbar_indicator`), off and greyed out while glass effects are on.
- Glass looks like Windows 7: dark caption text on a soft white glow, a bright colour over frosted wallpaper (the blurred wallpaper now sits under the titlebar and frame, `View::frost_`), tint strength as see-through-ness, two wide reflections, a deeper taskbar tint, a redder close button, and a soft drop shadow around floating windows (`window_shadows` in theme.toml, Settings -> Theme). docs/WINDOWS7_FRAME_REFERENCE.md, fourth pass.
- File manager: built-in FTP, FTPS, SFTP, WebDAV and SMB clients (`fleetwm-netfs`, FUSE with libcurl, libssh2 and libsmbclient); GVfs stays the fallback and handles the other protocols. Checked against real vsftpd, OpenSSH, Apache mod_dav and Samba.
- Chromium crashed in `xkb_state_update_mask` when a window was focused before the first key press: wlroots sends the focus-enter modifiers even when the seat has no keyboard, and no keymap had been sent yet. The seat now gets a keyboard as soon as one is registered (`Server::ensure_seat_keyboard`).
- Every build is `-O3` (`optimization=3` in `meson.build`, `scripts/test.sh`).
- Window titlebars no longer rescale the application icon on every redraw (`kit::draw_app_icon`; callgrind: icon paint 64.3M -> 0.88M instructions, per titlebar render about -35%); the tray icons in the bar and the launcher result icons use the same cached copy, which is now stored on the icon surface and freed with it
- The compositor asks SDL2 programs for Wayland first (`SDL_VIDEODRIVER=wayland,x11`) so an SDL terminal no longer keeps Xwayland running (about 18 MB less on the dev VM)
- `kit::measure_text` caches its answers (20,000 calls 22.2 ms -> 0.7 ms in the bench) and `draw_text` converts the text to glyphs once (2,000 labels 11.4 -> 8.8 ms); output pixels unchanged
- Builds on Debian 12, 13 and 14: wlroots 0.18 to 0.20 (`meson.build`, `WLR_VERSION_NUM` checks), Debian 12 gets wlroots 0.18 built from source (`scripts/build-wlroots-from-source.sh`, `install.sh`); see `docs/DEBIAN_BACKPORT.md`
- Taskbar icons (pinned apps, window buttons, the media applet) are scaled once and kept (`kit::draw_app_icon`) instead of being resampled from the 48 px original with the best-quality filter on every repaint, which was two thirds of the idle taskbar's CPU on the Celeron laptop. Together with the next item the idle bar went from 1.12% and 1.20% of a core to 0.15% twice (laptop's own bar, theme and wallpaper settings, nested compositor, fluctuating CPU load).
- The bar no longer redraws itself every 2 s for CPU/RAM/GPU/Disk text nobody can see: with the taskbar's Metrics element hidden (the default) a change of those figures used to repaint the whole taskbar (about 35 ms of CPU on the Celeron laptop). Measured with the laptop's own bar, theme and wallpaper settings in a nested compositor and a fluctuating CPU load: 1.02% and 1.12% of a core before, 0.35% twice after (the rest is the one-second clock strip).

- Volume and brightness keys with an on-screen card (`fleetwm-osd`, `apps/osd`): a rounded card at the bottom of the screen with a big speaker or sun icon and a 16-segment bar, shown for about 1.6 s. Handled in the compositor for XF86AudioRaiseVolume/LowerVolume/Mute and XF86MonBrightnessUp/Down. One resident process per session (socket in `XDG_RUNTIME_DIR`), quits 10 s after the last key. Brightness through sysfs or logind; the logic (commands, steps, wpctl and backlight parsing, lit segments) is in `src/common/osd_logic.*` with unit tests.
- Drag frame cap: `FLEETWM_DRAG_FPS` (5..240, default 24) sets it for experiments; resize requests to the client are paced to the same rate and the last size is sent when the grab ends (`FLEETWM_RESIZE_COALESCE=0` turns pacing off). Measurements in `docs/PERF_LOG_2026-10-11.md`.
- Task Manager: memory figures follow `free` (used = total - available, Shared, Buff/cache, Available); hover explanations for the CPU, memory, GPU and cache figures; a Memory traffic row from the memory controller counters (needs root or `perf_event_paranoid` <= 0).

- The file manager no longer has an options window of its own: Ctrl+, and Organize > Folder and search options open Settings -> File Manager. Open file manager windows follow changes to `fleetfm.toml` and `theme.toml` made in Settings while they run (they used to read them once at start). The file manager's colour scheme now defaults to "Follow the Fleetwm theme".
- The PGO training run says which helper programs are missing (wtype, wlrctl, foot, grim, PipeWire tools) and runs `fleetwm-config` export and import.
- `fleetwm-config export FILE` / `import FILE` and a Backup section in Settings -> About: the whole `~/.config/fleetwm/` folder as one tar.gz (`manifest.toml` with a format number, plus the toml files). Import checks every name, the manifest and that each file parses before changing anything, keeps the old folder as `fleetwm.bak-DATE`, and replaces files by rename so the running programs pick them up through their file watching. `outputs.toml` (monitor names) and `fleetfm-places.toml` (server addresses, user names) are left out unless `--all`. There are no Wi-Fi or Nextcloud credentials in the folder: the file manager never saves passwords, and Wi-Fi lives in wpa_supplicant. Archives with a newer format number are refused (`src/common/config_backup.*`).
- Window titlebar defaults follow Windows 7 measurements (docs/WINDOWS7_FRAME_REFERENCE.md): height 30, buttons 28x20 with a 47 px close button, title left-aligned. Existing `theme.toml` values are kept.
- Glass (translucent frame, taskbar, menus) is on by default; window corner radius 4 like Windows 7; the window's outer line is continuous from the titlebar down the side strips and round the bottom corners.
- Settings -> Default Apps -> Terminal keeps the arguments of the entry you pick: choosing "Lestrix Lite" (`lestrix --lite`) saved only `lestrix`, so the next start of Settings showed the plain entry selected and the shortcut opened the full version. The saved command is split into words when the terminal is started (it used to be run as one program name).
- Programs started from the compositor, the bar, the launcher or Settings no longer inherit blocked SIGINT/SIGTERM/SIGCHLD or ignored SIGPIPE/SIGCHLD: Ctrl+C works in a terminal opened from the start menu, and `git clone` there no longer fails with "waitpid ... No child processes" (`src/common/child_signals.hpp`).
- `--version`/`-V` and `--help`/`-h` on every program (compositor, bar, wallpaper, launcher, lock applet, Settings, Shortcuts, language picker, power menu, audio mixer) print the version, the usage and "Copyright (c) 2026 Thomas Mozdren" with the project link https://github.com/BeanGreen247/fleetwm.
- Fleetwm's own windows (Settings, Shortcuts, language picker) have an opaque body again; with Glass effects only the frame (titlebar, borders), bar, taskbar and menus are translucent. The frosted backdrop behind those windows is retired.
- Window edge redrawn from real Windows 7 screenshots: a dark outline with a light line inside it, both following corners of radius 6 (was 4, with a clipped straight line that looked cut off).

### Added: desktop icons and the desktop menu (`fleetwm-desktop`)
- Desktop layout: the icons of `~/Desktop` plus Computer, Home and Trash, and the Windows 7 right-click menu (View, Sort by, Refresh, Paste, New, Display settings,
  Personalize) with "Show desktop icons" to hide them. Tiling layout: no icons and no menu, a small card with the keys for the shortcut list (`docs/DESKTOP.md`).
- File manager: clipboard and drag and drop with other programs; a plain drop asks Copy (default), Move, symbolic link or hard link; Windows 7 style right-click menus with
  submenus and the Linux extras (Open with, Send to, Compress, Extract, Copy location, terminal here); Group by; preview pane; tooltips; Undo; computers on the network.
- PGO: `fleetwm-fm --train DIR` and a training phase for the file manager and the desktop menus.

### Added: file manager (`fleetwm-fm`)
- A native C++ file manager, GTK-free like the rest of Fleetwm (`docs/FILE_MANAGER.md`): Windows 7 Explorer layout and behaviour by default, switchable
  to Windows 10, Mac Finder, Caja, Nautilus, Nemo, Thunar, PCManFM and Dolphin styles; tabs; breadcrumb address bar; search as you type; all eight view
  modes; thumbnails; Computer view with free-space bars; trash that follows the freedesktop.org specification; properties and checksums; a Folder Options
  dialog with every setting saved live to `~/.config/fleetwm/fleetfm.toml`. Icons, including the application icon, are drawn in code.
- Copy and move check their result: each file is written under a temporary name, flushed, read back from the device (`O_DIRECT`) and compared by checksum
  with the source before it gets its real name; a move removes the original only after that. Automatic checksum choice (SHA-256, SHA-512 or BLAKE2b, whichever
  is fastest on the machine). Windows 7 style progress card with speed, time remaining, pause, cancel and a speed graph.
- Safe eject: refuses while a transfer or a program uses the drive (and lists the programs), flushes, unmounts through udisks, powers the disk off, and says
  "can now be safely removed". Unmounted removable partitions are listed and mount with a double-click.
- Network places through GVfs: SMB, SFTP/SSH, FTP, WebDAV including Nextcloud and ownCloud, NFS, AFP, MTP, AFC, cameras. Not run against real servers here.
- `fleetwm-fm --screenshot` renders the window without a compositor; `--write-icon` writes the icon; `docs/FILE_MANAGER_PERFORMANCE.md` has the measurements.
- A folder of 100,000 files is listed and sorted in 84 ms (232 ms first, std::filesystem 438 ms): sizes and dates are read for the drawn rows only and names sort
  by precomputed keys. A verified copy of 20,000 small files takes 4.8 s instead of 529 s (one flush per batch, parallel read-back). 25 to 35 MB resident, no idle CPU.
- `src/common/child_signals.hpp` includes `<initializer_list>` (it failed to compile in a plain debug build).

### Added
- Start-up prewarm: every program learns once which pages of which files it uses while starting, and the compositor asks the kernel to read
  those ahead before it starts the bar, the wallpaper and the lock applet (`~/.cache/fleetwm/prewarm/`, off with `FLEETWM_PREWARM=off`).
  On the dev VM the whole cold-start penalty of the bar is 3 ms, so the gain is small there; it is meant for slow disks.
- Glass effects now frost Settings, Shortcuts and the language picker too: the compositor shows the blurred wallpaper behind the window
  (it follows the window when it moves, resizes or changes screen) and the window paints a see-through background. The Tiling borders
  are see-through with Glass effects on.
- More than one screen: every screen gets its own bar and wallpaper (started and ended with the screen), windows open on the screen with the
  pointer, the taskbar of a screen lists its windows, windows follow when dragged across the edge, and the windows of an unplugged screen
  move to another one and come back when it returns. Workspaces are shared by all screens in the Desktop layout.
- Window corners: the outer corners of a free-floating window are rounded (radius 7) when the theme's corner style is rounded; maximized
  and snapped windows stay square.
- Tooltips of the bar are frosted like the bar when Glass effects is on.
- Caption buttons show their pressed look while held and act when released over the same button (released elsewhere cancels),
  like Windows.
- Hover tooltips on the bar's metrics are richer and update every second while shown: CPU (each core's load and clock,
  scheduler latency, power draw from the battery or the CPU energy counter), memory (used, free, available, cached,
  buffers, swap), GPU (core usage, power, core and memory clock, VRAM; AMD through sysfs, NVIDIA through `nvidia-smi`,
  Intel clock only), disk (read and write speed of all disks).
- The busy pointer (wait, progress) spins: twelve cached pictures at twelve per second, a timer that runs only while one is shown.
- Settings -> Mouse: "Primary button" (left or right) and "Natural scrolling", saved in `mouse.toml` and applied to every
  libinput mouse and touchpad, including ones plugged in later.
- Settings -> Theme: "Window frame (px)" (0-16).
- The volume icon shows a muted sink (red slash, "(muted)" in the tooltip).

### Changed
- Thicker window borders by default: the Desktop frame 6 -> 8 px, the Tiling focus border 2 -> 4 px and the pinned border 3 -> 4 px.
  A theme.toml that already holds a value keeps it: change them in Settings -> Theme (Window frame, Focus border, Pinned border).

### Fixed
- install.sh: apt can no longer wait unseen. needrestart is told to ask and restart nothing, the dpkg lock is waited for, mirrors time
  out and retry, and a step with no new output for two minutes prints what it may be waiting on.
- Settings, Shortcuts and the language picker open centred in the work area, titlebar and frame included; at 1024x768 the
  Settings window used to reach 8 px under the taskbar. Long help lines in Settings now wrap instead of running off the page.
- A headless or nested output now takes any size as a custom mode (`OUTPUT_SET` and `outputs.toml` were refused),
  which made 1024x768 testable.
- The seat offers the pointer from the start, so a session without a keyboard (headless) gives clients a pointer.

## 0.4.0 - 2026-10-07

### Fixed
- A plain `kill` ended Settings, the bar and the mixer without a clean exit when PipeWire was running (PipeWire's thread
  started before SIGTERM was blocked). Every program now blocks it first thing in `main()`.

### Added
- Mouse settings (Settings -> Mouse): a pointer speed slider with the same eleven steps as Windows (6 is the default)
  and "Enhance pointer precision" (pointer acceleration on or off), saved in `mouse.toml` and applied to every mouse
  at once, including ones plugged in later. A preview shows the pointer shapes.
- The mouse pointer is drawn by Fleetwm in the Windows 7 style: the Aero pointers with a soft shadow and the teal glass
  busy ring when Glass effects is on, the same shapes flat (solid white, black outline, no shadow) when it is off. All
  14 shapes (arrow, help, working in background, busy, text, link hand, precision cross, not allowed, move, the four
  resize arrows, alternate select) are drawn once per style and scale and kept; toggling glass redraws them.
  Programs that load their own cursor theme still show that theme.

### Changed
- Battery icon: a plug replaces the charging bolt on AC power, the fill sweeps from left to right while charging, is
  green when full, red under 10% and the icon colour otherwise (the percentage is shown beside the icon).
- Network icons in the Windows 7 style: Wi-Fi as five rising signal bars (white when lit), wired as a small computer
  with a cable, and a new mobile data icon (bars with an antenna mast). Mobile data modems (wwan devices and
  NetworkManager modems) are listed in the bar tooltip and in Settings -> Network.
- Windows 7 style window frame in the Desktop layout: windows with a titlebar get a frame down both sides and along the
  bottom (6 px by default, `frame_px` under `[titlebar]` in `theme.toml`, 0 turns it off). The titlebar (drag area
  with the pin, minimize and maximize buttons) is as wide as the window and has no border of its own: the frame
  starts under it. It follows Glass effects: translucent glass with a light outer rim, or flat and opaque. Maximized
  and fullscreen windows have no frame, the frame is part of the resize handle, and snapping, maximizing and the
  cascade take its width into account.
- Volume is a speaker icon (more waves as it gets louder, a red slash when no volume can be read) with no percentage
  text; hover it for the percentage.
- Desktop taskbar order on the right: tray, keyboard layout, volume, network, power mode, battery, then the clock last.
  The power button is gone from the taskbar (Shut down is in the start menu).
- The bar's custom icons (battery, plug, network, volume, power mode) are drawn once per state and copied afterwards
  (`src/fleetkit/glyph_cache.hpp`): bar CPU with a charging battery animating on the Celeron N4020 fell about 11-13%
  (best of 3: 288 ms to 251 ms per 40 s).
- The profile-guided build's training run is wider and shorter: 85 s instead of 150 s, covering every icon state, all
  themes, frame widths, bar layouts, taskbar edges, window drags, resizes and snaps, caption buttons, every virtual
  desktop and the Tiling borders (functions run 33.5% to 40.1%, compositor 46.7% to 60.5%, bar 67.6% to 80.7%).
  Its keyboard chords had never worked (modifiers must be sent with wtype's `-M`); that is fixed.
- Power mode icon is a tachometer: needle low for power saver, in the middle for balanced, pegged in the red zone for
  performance.

## 0.3.0 - 2026-10-06

### Added
- Performance section in the README and a 2026-10-06 section in docs/OPTIMIZATIONS.md (measurements, what other
  compositors do, rejected ideas).
- Glass effects setting (Settings -> Theme, `glass_effects` in `theme.toml`): a frosted copy of the
  wallpaper behind translucent surfaces, with a tint, a soft sheen and a light rim. Used by the bar in
  every layout, the taskbar, window titlebars, the start menu and Alt+Tab.
- Windows 7 style start menu: program list and search on the left, places and Shut down on the right.
- Windows 7 style Alt+Tab switcher with window thumbnails.
- Per-GPU readout in the bar (AMD busy percent, Intel RC6 idle time, NVIDIA through `nvidia-smi`).
- `CHANGELOG.md`.

### Fixed
- The installer's unity build failed on the first 0.3.0 tag (`kPad` defined in both the overlay and the Alt+Tab
  switcher); the overlay's file-scope names are prefixed, and a second latent clash (`focused_view` in
  `input.cpp` and `output.cpp`) is renamed.

- Power menu: Sleep, Reboot and Shut down work without a password for whoever is at the keyboard. The installer
  now ships `50-fleetwm-power.rules` (logind's own rules ask an administrator whenever another user is logged in,
  and some distributions differ). New unit tests run the rules in node and read the installer to be sure every
  shipped rule is installed (`scripts/test.sh power`).
- Power menu icons redrawn as one outline set on a 24 px grid: the old set mixed a filled moon with thin outlines,
  sat off centre and had a glitchy reboot arrow. Tests check size, weight and centring.

- The profile-guided build's training run is much longer and wider: 150 s instead of 20 s, on the virtual
  screen, cycling six look/layout combinations (Tiling and Desktop, glass on and off, dark and light) with
  every Settings page, the start menu and launcher search, the power menu, shortcuts and language windows,
  Alt+Tab, snap keys, the performance overlay, layout switching, terminals scrolling output, screen capture,
  pointer sweeps and the compositor's IPC queries (key, pointer, terminal and capture steps run when
  wtype, wlrctl, foot and grim are installed, which the installer does). `PgoTraining` tests keep it that way.
- Caption buttons now take their colours from the theme, so they fit a dark, an OLED black and a light theme, with
  glass on and off (the first version was fixed light grey-blue and looked like bright slabs on a dark glass bar).
  Same shape, size and placement. The glass is made from the titlebar's own colour raised a little towards the text
  colour and tinted by the accent; glyphs use the theme's text colour with a contrasting outline (white on the red
  close button, which is a deeper red on a dark bar); the hover and the glow use the accent colour (orange behind
  close); in glass mode the buttons are translucent like the bar. Tests for dark and light themes, accent-driven
  hover, glass versus matte, and a mutation check that ignoring the theme fails.
- Windows Aero (glass) performance: the glass titlebar background and the caption strip are drawn once and
  copied (`src/fleetkit/titlebar_draw.cpp`), because a titlebar is redrawn every time the pointer enters or
  leaves a button. A redraw with hover moving across the buttons went from 342 us to 51 us (glass, 800 px wide;
  the glass background alone from 103 us to 4 us, and from 242 us to 9 us at 1600 px, so glass now costs the
  same as matte). Both caches are byte-capped LRUs (2 MB and 1 MB). Settings: the glass row is now named
  "Glass effects (Windows Aero)".
- Window caption buttons restyled as a joined Windows 7 style strip (pin, minimize, maximize/restore, close),
  the same in glass and matte mode in the Desktop layout: light two-tone glass buttons and a red close button
  that is 1.6 times as wide, hanging from the top-right corner of the window with rounded bottom corners, a dark
  outline with a white inner edge, white glyphs with a dark outline, a blue glow behind a hovered button and an
  orange one behind close, a pushed-in blue pin when the window is pinned, and faded buttons on an inactive window.
  Drawn with cairo paths and gradients (`src/fleetkit/caption_buttons.cpp`), no images. The layout has a strip mode
  (`TitlebarMetrics::strip`) for the touching, top-flush buttons and the wider close button; hit testing follows it.
  Tests: `TitlebarStrip` (geometry) and `CaptionButtons` (rendered pixels, both modes).
- Volume mixer: it opens next to the bar or taskbar it came from (bottom right above a bottom taskbar, and so on)
  instead of always at the top right, which in the Desktop layout was the other side of the screen; it was also
  placed with a wrong bar height (24 where the bar is 30). Clicking anywhere outside it closes it (the compositor
  does that, like the start menu), and launching it again closes the open one instead of stacking another (dozens
  piled up). Tests: `PopupSpot`, `SingleInstance`, `PopupDismissal`.
- Sound without a reboot: after installing the device profiles the installer runs `alsactl init` (applies the
  profile's BootSequence: output mixers on, volumes), saves it (`alsactl store`) and restarts the user's sound
  services. Found on the test laptop (Intel SOF/ES8336): PipeWire showed Speakers but nothing played until the
  boot sequence ran, then a tone through the speakers was picked up by the built-in microphone at 100 times the
  noise level, and the owner heard it. New `docs/AUDIO.md` covers the layers, how other desktops divide the work
  and how to check sound.
- No sound on some laptops: the installer now also installs `pipewire-pulse` (the PulseAudio-compatible server
  browsers and players use), `pipewire-alsa`, `alsa-ucm-conf` (device profiles: an Intel SOF/ES8336 laptop got
  the silent "stereo-fallback" profile without it and showed only a generic stereo sink) and `alsa-utils`. On the
  test laptop PipeWire then showed Speakers, three HDMI outputs and the analog microphone on the HiFi profile.
- The training run now puts the freshly built programs first on `PATH` (`scripts/pgo-path-shim.sh`), so the bar,
  wallpaper, padlock, launcher, Settings, power menu and the rest that the compositor and the desktop start by
  name are the instrumented ones: before, a machine with Fleetwm already installed profiled nothing for them
  (the old copies ran), and a fresh machine started nothing (the names resolved to nothing). Programs the
  compositor did not autostart are started once, never twice. Tests run the shim against a fake build tree.
- The polkit rule behaviour tests no longer skip on a machine without node: they carry a small evaluator and
  compare it with real JavaScript when node is there.

### Added (tooling)
- `scripts/check-unity-collisions.py` (also runs with `scripts/test.sh` and `meson test --suite static`) and
  `scripts/check-unity.sh` (a real unity build that reports every error, then the unit tests) so a clash like
  that is caught before a tag. Release checklist in `docs/DEVELOPMENT.md`. `install.sh` runs the quick check right
  before the slow build and stops with the clashing names instead of failing minutes into the compile.

### Changed
- Higher contrast for bar icons and text.
- Settings, Shortcuts and the language checklist stack like normal windows in the Desktop layout.

### Performance
- Measured on the Celeron test laptop and recorded in `docs/OPTIMIZATIONS.md`: a scrolling full-window terminal
  keeps the GPU about 30% busy and costs the compositor about 1.1 ms of CPU per frame (a third of it the copy of
  the client's pixels to the GPU); a compositor IPC round trip is 37 us; idle wakeups are about 1 a second for
  the compositor and 4.5 for a bar with a seconds clock. Tried and rejected, with numbers: `MESA_NO_ERROR`,
  `mesa_glthread`, paging out Mesa's idle LLVM library, prewarming apps, a newer wlroots.
- The performance overlay repaints in 0.5-0.6 ms (it was 1.0-1.2 ms): the panel is copied from a cached picture,
  graph bars are written straight into the pixels, and CPU use is one `getrusage` call.
- The performance overlay (Alt+Shift+I, Ctrl+Alt+I in Desktop) is one small cairo-drawn picture instead of
  ~665 scene rectangles, is repainted at most four times a second inside the frame being committed, and no
  longer keeps the compositor rendering 60 frames a second by itself. On the Celeron laptop the compositor
  cost with it on fell from 58% to 0.3% idle and from 38% to 6.5% under a busy terminal. It now also shows
  what the compositor spends per frame, late frames, CPU and current (not peak) memory.
- The bar's seconds clock repaints only the clock strip and tells the compositor only that strip changed,
  instead of redrawing and re-damaging the whole bar every second.
- Glass rectangles are painted once and reused (small LRU cache, 3 MB cap), so a redraw is one blit
  instead of a clip, a stretched backdrop copy, two gradients and two strokes.
- The blurred backdrop is decoded once per process, not on every Alt+Tab step.
- The start menu surface is now just the card and its shadow instead of a full-output surface, so it needs
  far smaller buffers; the compositor closes the menu on a press anywhere outside it.
- The compositor keeps its linux-dmabuf object and gives it to the scene so GPU clients get scan-out
  format feedback (direct scan-out of fullscreen GPU buffers); wp_viewporter and wp_single_pixel_buffer are
  enabled.
- The Island bar re-sends its layout only when its width really changes, so a ticking clock no longer
  sets surface size, margins and anchor every second or dismisses a hover tooltip.

## 0.2.0 - 2026-10-06

### Added
- Network support: Settings -> Network tab (join, disconnect, forget, password entry), bar network icon
  with hover tooltip, NetworkManager and wpa_supplicant backends and a read-only sysfs view.
- Keyboard layouts: `keyboard.toml`, Win+Space / Win+Shift+Space / Alt+Shift switching, Settings ->
  Keyboard tab, bar layout pill and the language checklist (`fleetwm-langpicker`).
- Keep-awake padlock in the tray (`fleetwm-lockapplet`), tray hover tooltips.
- Super key shortcuts, configurable combo shortcuts, Windows-style snap keys, Alt+arrow focus moves.
- X11 programs run as normal windows through XWayland.
- Settings -> Power page, Settings -> About with version and git revision, `fleetwm --version`.
- Installer: numbered steps with an explanation of each, a live `[x/y]` progress line, a summary of stage
  times, Mesa/Vulkan/VA-API drivers matched to the GPU, Debian non-free repositories.

### Changed
- User-facing programs moved to `apps/` (audiomixer, launcher, powermenu, settings, shortcuts,
  lockapplet, langpicker); bar, wallpaper, locker and greeters stay in `src/`.
- GTK, Chromium and Qt apps follow the Fleetwm dark/light theme.
- Release build flags favour speed; the compositor runs at raised priority.

### Fixed
- foot started from the launcher or start menu gets Fleetwm's terminal config.
- reboot, poweroff and halt work in session terminals.
- Cursor theme fallback and cursor-shape-v1.
