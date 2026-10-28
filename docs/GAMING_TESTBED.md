# Gaming test bed and performance notes (running log)

Started 2026-10-28 on the gaming laptop (Ashblade, Nobara 44, Ryzen 7 5800H, AMD iGPU draws the desktop, RTX 3050 Ti for games, TuxThrottle installed, falcond running).
This file is the running record of what was tried, what works, what does not, and what belongs in TuxThrottle later. Keep it current after every step;

## Plan (the owner's list, 2026-10-28)
1. Properties dialog: a shortcut/program opens straight away (Open / Open file location buttons), like Windows. [x] done (`dialog_properties`)
2. osu!lazer (native, `osu-lazer.desktop`, tools in ~/osu-lazer-tools): stutter, and a lag spike when leaving the game normally. Investigate latency under Fleetwm. [ ]
3. Steam test beds, 5-10 minutes each, bugs and rendering issues fixed along the way: GTA III DE (1546970), LEGO Star Wars Skywalker Saga (920210),
   South Park Stick of Truth (213670), GTA V Enhanced (3240220, story mode only), Need for Speed Heat (1222680, the EA title), Serious Sam 4 (257420),
   Mafia (40990, the old game), LEGO Batman The Videogame (21000). [ ]
4. Heroic (GOG/Epic): Loria, Rayman 2, Rayman 3, Rayman Origins. [ ]
5. Other launchers integrate with Fleetwm (Lutris, Heroic, Bottles) and Battle.net (WoW) via Lutris (`net.lutris.battlenet-1.desktop`). [ ]
6. Overlays: MangoHud, vkBasalt, obs-vkcapture, Steam overlay. [ ]
7. Bug: Steam's MangoHud takes priority over the native install's config; must not unless asked. [ ]
8. Per game: what TuxThrottle says (its gate files, launch options, MangoHud tab), linking and Proton/Wine issues; Fleetwm as a Wine/Proton backend if sensible. [ ]
9. Final pass over all games: works / does not. Notes for TuxThrottle at the end. [ ]

## Done earlier this session (all pushed, see CHANGELOG.md and git log)
Per-GPU Task Manager and taskbar, borderless X11 games fullscreen, KWin InputDevice bridge for TuxThrottle's touchpad switch, built-in XEmbed tray (Wine icons, ARGB),
dbusmenu tray items (Steam), file manager runs shortcuts/executables, Properties checksums fit, VRR commit folded into the frame commit, TuxThrottle readout auto-refresh,
debounced game boost with pre-game power profile restore, calmer glass ray, tinted start orb (cached: 0.47 ms -> 2 us), per-game MangoHud/GPU switches in Settings,
every installer builds PGO only, installer root-owned build files fix.

## Tools added for this (2026-10-28)
* `fleetwm-ctl frame-stats [reset] | windows | close ID | raw CMD` (apps/ctl) and the compositor's always-on frame pacing (`FRAME_STATS` IPC, src/compositor/frame_stats.hpp): interval p50/p95/p99/max, late (>1.5 refresh) and hitched (>3) frames, the compositor's commit cost. NEEDS A RE-LOGIN after installing: the compositor process on the laptop was started before these existed, so `frame-stats` prints nothing there yet.
* `scripts/game-testbed.py` (one game: launch or --attach, watch, screenshots every 30 s with a black-screen check, frame pacing, close, exit pacing, report in perflog/testbed/<date>/) and `scripts/game-testbed-all.sh` (the Steam list, 6 minutes each). The tests use the real screen: the owner must not be playing.

## Results log
* 2026-10-28 09:36 GTA III DE (attached to a running game): renders normally (no black screen in 9 screenshots, in-game 60 fps counter, NVIDIA 60-80 % busy, 83-88 C), stayed up 4.7 minutes until it was closed. NOTE: the owner was playing it at the time (the screenshots show their save), so the run was an observation of their session, not a controlled test; the earlier black screen did not reappear with the build that has the VRR commit fix.

## Native Steam games, first pass (2026-10-28, installed build a721aab but the compositor process was from before: no frame stats)
Harness now reports per game: GPU used (nvidia-smi pmon + amdgpu fdinfo), CPU/RAM/NVIDIA sm and VRAM per 10 s, install/error/Steam dialogs (screenshots, error dialogs closed), leftover processes stopped.
* PASS: Counter-Strike: Source, Half-Life 2: Deathmatch, Half-Life Deathmatch: Source (64-bit, Source "Vulkan - 64 Bit" windows).
* Brutal Legend: "Unsupported GL Extension" dialog, then `Buddha.bin.x86` segfault (32-bit, core dump in coredumpctl). CS:GO legacy: "Error!" then `csgo_linux64` segfault in libtier0_client.so. Garry's Mod: "Error!" dialog.
* Half-Life 2 / Episode 1 / Episode 2 / Lost Coast / Half-Life: Source: the game process RUNS (hl2_linux, SteamGameId set) but no window ever reaches the compositor; the Steam log says "SDL video target is 'wayland'". A 32-bit SDL-Wayland + NVIDIA GL window that never maps: a real bug to chase (candidates: the forced PRIME offload launch options, 32-bit wl_egl/NVIDIA EGL, our dmabuf feedback for a client on the dGPU).
* LESSON: Steam runs one game at a time. The stuck HL2 stayed alive and made every later launch (OpenTTD, Portal, POSTAL 2, TF2, Witcher 2) do nothing, so those five results are INVALID, not game failures. Fixed in the harness (`kill_steam_games()` before and after each test).
* Steam shows a "Select launch option" dialog for games with several options (TF2: Play / Legacy OpenGL; POSTAL 2): it needs a click on Play; `wtype -k Return` did not answer it. Open: click via the virtual pointer, or set "Always use this option" once per game.
* OpenTTD: "Install library folder not found" in the Steam log: its manifest points at a library folder Steam cannot find (not a Fleetwm issue).

## Second pass (harness with dialog answering, 11:00-11:35)
* Dialogs: the harness now answers Steam dialogs (OCR with tesseract, installed on the laptop by dnf for this; a real pointer click through `fleetwm-pointer`, scripts/pgo-pointer.c built into ~/.local/bin, because XTEST clicks do not reach Steam's dialogs under Xwayland): licence -> Accept/Agree, install -> the Voidstride library then Install, launch options -> Play. Verified on TF2's "Select launch option" dialog. Install and licence handling are written but have not met a real dialog yet.
* TF2 starts after the click: `tf_linux64`, "Team Fortress 2 - Vulkan - 64 Bit".
* DiRT Rally / DiRT Showdown: the game process starts and ends about 3 s later, no window, no core dump; the log shows `DirtRally.sh: 9: [: ... unexpected operator` (Feral's launcher script under /bin/sh in the runtime). Probably not Fleetwm; unproven.
* HALF-LIFE 2 FAMILY / PORTAL (32-bit Source, SDL2 Wayland + EGL on the NVIDIA card): the process runs for minutes (15 % CPU, compiling D3D9 shaders through ToGL, libEGL_nvidia + libnvidia-egl-wayland2 + libwayland-egl loaded) but NO window is ever mapped in the compositor, no error in the session log. Strongest lead: the game renders on the NVIDIA card (forced PRIME offload launch options) while the compositor runs on the AMD iGPU and, since the screenless NVIDIA card is excluded from WLR_DRM_DEVICES, knows only the AMD device: the NVIDIA client's dmabuf/explicit-sync buffers are never accepted, so the first frame never arrives. 64-bit Vulkan titles on the same setup do appear (CS:S, TF2, HL2DM). To test: (a) run one of them with `SDL_VIDEODRIVER=x11` (Xwayland path) or without the NVIDIA offload variables; (b) keep the NVIDIA device in the compositor's device list (dmabuf feedback with the dGPU as a secondary device) and see whether wlroots then accepts its buffers.

## NVIDIA clients on Wayland never map (narrowed down, NOT fixed)
Reproduced without any game: `es2gears_wayland` with the NVIDIA EGL vendor (`__EGL_VENDOR_LIBRARY_FILENAMES=.../10_nvidia.json __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia`) and `vkcube --wsi wayland` on the NVIDIA ICD create their surface, get and ack the configure, set the opaque region and then NEVER attach a buffer (WAYLAND_DEBUG=1; no `wl_surface.frame`, no dmabuf `create_params`); the same program on Mesa/AMD attaches at once. The compositor sees nothing, so no window. Checked and ruled out: the legacy `wl_drm` global (removed in a test build: no change), explicit sync (`FLEETWM_NO_SYNCOBJ` test build: no change), `EGL_PLATFORM=tty` in the ssh shell (unset: no change), nvidia-drm modeset=1 fbdev=1 is set. The client main thread idles in poll while an NVIDIA EGL worker waits on a condition; gdb shows no symbols. KDE (KWin) maps the same clients on this laptop. Next candidates: dmabuf feedback content/version (we offer v4, 1198 formats, one tranche, no per-surface feedback), the NVIDIA device missing from the compositor's DRM device list (`WLR_DRM_DEVICES` leaves the screenless NVIDIA card out), a missing event the NVIDIA driver waits for (compare with a KWin or sway log under the same client), or running the compositor with both cards. 64-bit Vulkan titles that pass (CS:S, TF2, HL2DM) are probably going through Xwayland.
Update: Fleetwm did not offer `wp_fifo_manager_v1` / `wp_commit_timing_manager_v1` (KWin and Mutter do; wlroots 0.20 has no implementation). The NVIDIA client binds both the moment they exist and then calls `get_fifo` / `get_timer` on its surface, so they are looked for; they are now offered (accepted, not enforced: `src/compositor/presentation_hints.cpp`, test builds only so far). It still never attaches a first buffer in the nested test (a nested compositor on the host's AMD GPU, which is not a faithful test), so the real cause is still open. Untried: a run of the real session with both GPUs in `WLR_DRM_DEVICES`, per-surface dmabuf feedback, dmabuf v5 (we offer v4), the NVIDIA card being in runtime suspend during the first call (it wakes in seconds, tests waited 5-8 s).
A cheap workaround that matches what the osu launcher already does: run NVIDIA-offloaded games through Xwayland (`SDL_VIDEODRIVER=x11` in their launch options), where the GLX/Vulkan-X11 copy path works.

## Findings so far
* TuxThrottle gates MangoHud: `/usr/local/bin/mangohud` (script, "tuxthrottle-mangohud-gate") only lets a Steam game show the overlay when
  `~/.config/tuxthrottle/mangohud-games/<appid>.on` exists; `<appid>.dgpu` forces the NVIDIA card. Settings -> Gaming has both switches.
* Every Steam game's launch options carry `__NV_PRIME_RENDER_OFFLOAD=1 __VK_LAYER_NV_optimus=NVIDIA_only ... gamemoderun mangohud %command%`.
* falcond (Nobara) also boosts and restores the power profile it saw at game start; ppd was `power-saver` since boot, so falcond restored `power-saver` (state.ini written at the falcond deactivation).
* Mount and Blade (22100): exits by itself after ~13 s; prefix "Upgrading prefix from GE-Proton11-6 to 11.0-100 ... invalid version".
* The test rig on the gaming laptop must never leave TuxThrottle's Game Mode on (check `tuxthrottlectl status --json`).

## osu!lazer (read from the launcher, not yet measured)
* `/opt/tuxthrottle/osu_lazer_tools/osu-lazer-launcher.sh` (config `~/.config/osu-lazer-launcher/config`): USE_DGPU=yes, so osu! runs on the NVIDIA card through Xwayland (`SDL_VIDEODRIVER=x11`, PRIME offload) while the AMD iGPU draws the desktop; its own header says this "adds a frame copy (more latency) and can cause rendering glitches". `FrameSync = Unlimited` and driver vsync off make osu! draw as fast as it can, and every frame is copied across to the iGPU and composited. A likely source of the lag, to confirm with `fleetwm-ctl frame-stats` after a re-login (compositor CPU while osu! runs, hitched frames).
* The launcher sets `performance` with `powerprofilesctl` and restores the old profile at exit with `powerprofilesctl set <old>` (a Python start-up, a CPU spike at the moment of exit), changes the PipeWire quantum, and calls `kwin_set` on `org.kde.KWin.InputDevice` to switch pointer acceleration to flat. Fleetwm's KWin bridge had no `devicesSysNames` list and no `pointerAccelerationProfileFlat`, so acceleration was never switched off under Fleetwm: added now (both, plus `pointerAcceleration` speed); untested on real hardware.
* To measure: run the launcher, play a map, quit with the menu, with `fleetwm-ctl frame-stats reset` before and `frame-stats` right after quitting; compare with `USE_DGPU=no`.

## For TuxThrottle later
* Game Mode helper scripts (`gaming-balanced`) always set ppd `balanced`; they should restore what was in force before `gaming-performance` ran (Fleetwm now does this itself, a TuxThrottle-side version would stop the two fighting). falcond restores its own snapshot 3 s after the last process, which can overwrite either.
* The osu launcher and Fleetwm both change the power profile: whoever restores last wins.
* `tuxthrottlectl gamemode on/off` is slow (sudo scripts, ~5 s) and cannot be asked "is it on" cheaply from a script; `status --json` takes 0.3 s.
(filled as found)
