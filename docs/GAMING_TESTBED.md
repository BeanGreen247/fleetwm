# Gaming test bed and performance notes (running log)

Started 2026-10-28 on the gaming laptop (Ashblade, Nobara 44, Ryzen 7 5800H, AMD iGPU draws the desktop, RTX 3050 Ti for games, TuxThrottle installed, falcond running).
This file is the running record of what was tried, what works, what does not, and what belongs in TuxThrottle later. Keep it current after every step; the memory
index (`~/.claude/projects/-home-bean-git-fleetwm/memory/`) points here.

## Plan (the owner's list, 2026-10-28)
1. Properties dialog: a shortcut/program opens straight away (Open / Open file location buttons), like Windows. [ ]
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

## Findings so far
* TuxThrottle gates MangoHud: `/usr/local/bin/mangohud` (script, "tuxthrottle-mangohud-gate") only lets a Steam game show the overlay when
  `~/.config/tuxthrottle/mangohud-games/<appid>.on` exists; `<appid>.dgpu` forces the NVIDIA card. Settings -> Gaming has both switches.
* Every Steam game's launch options carry `__NV_PRIME_RENDER_OFFLOAD=1 __VK_LAYER_NV_optimus=NVIDIA_only ... gamemoderun mangohud %command%`.
* falcond (Nobara) also boosts and restores the power profile it saw at game start; ppd was `power-saver` since boot, so falcond restored `power-saver` (state.ini written at the falcond deactivation).
* Mount and Blade (22100): exits by itself after ~13 s; prefix "Upgrading prefix from GE-Proton11-6 to 11.0-100 ... invalid version".
* The test rig on the gaming laptop must never leave TuxThrottle's Game Mode on (check `tuxthrottlectl status --json`).

## For TuxThrottle later
(filled as found)
