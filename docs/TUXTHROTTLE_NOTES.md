# For TuxThrottle (and the osu! launcher): what the Fleetwm game test bed found

Measured on the gaming laptop (Ryzen 7 5800H, AMD iGPU draws the desktop, RTX 3050 Ti renders games, Nobara 44), 2026-10-28. Details and raw numbers: `docs/GAMING_TESTBED.md`.

## 1. osu!lazer: FrameSync = Unlimited makes the whole desktop lag (the "stutters like hell on exit" report)
`fleetwm-ctl frame-stats` (frames actually presented to the screen) while osu! runs through `osu-lazer-launcher` with `USE_DGPU=yes`:

| osu! FrameSync | frames presented | p99 interval | hitched frames (> 3 refreshes) per 10 s | NVIDIA |
|---|---|---|---|---|
| Unlimited (current default in framework.ini) | 2 per second | 590 ms | 17-19 | 99 % |
| Limit4x | 144 per second | 7.0 ms | 0 | 86 % |

Cause: an unlimited osu! on the dedicated card floods the compositor with buffers that have to be copied across to the integrated GPU; the NVIDIA card saturates and the compositor waits on it. Closing the game is worst. Fix: `docs/osu-lazer-framesync.patch` (against `/opt/tuxthrottle/osu_lazer_tools/osu-lazer-launcher.sh`): a `FRAME_SYNC` setting (default `limit4x`, `keep` leaves osu! alone) that sets `FrameSync` in `~/.local/share/osu/framework.ini` for the run and puts the old value back on exit, in the launcher's existing `restore_cmds` style. Not applied to the installed copy; osu!'s own settings file was restored to Unlimited after the test.

## 2. Game Mode scripts and the power profile
* `gaming-balanced` always sets ppd `balanced`; falcond puts back the profile it saw 3 s after the last game process; Fleetwm now restores the pre-game profile last (6 s after the last game window). Whoever acts last wins: a TuxThrottle-side "remember what was set before `gaming-performance`" would end the fight.
* `tuxthrottlectl gamemode on|off` takes about 5 s (sudo helper scripts) and `status --json` 0.3 s; there is no cheap "is it on" call.

## 3. MangoHud gate
`/usr/local/bin/mangohud` only lets a Steam game show the overlay when `~/.config/tuxthrottle/mangohud-games/<appid>.on` exists (and forces the NVIDIA card with `<appid>.dgpu`). It is invisible to the user: a `mangohud %command%` launch option "does nothing". Fleetwm's Settings -> Gaming lists the installed games with both switches; TuxThrottle's own tab could say "off for this game" next to the launch option.

## 4. Every game's launch options force NVIDIA offload
`__NV_PRIME_RENDER_OFFLOAD=1 __VK_LAYER_NV_optimus=NVIDIA_only __GLX_VENDOR_LIBRARY_NAME=nvidia ... gamemoderun mangohud %command%` on all 90 games. That is right for demanding games and wrong for old ones: Brutal Legend ("Your GL driver does not support ARB_half_float_vertex": the game's check breaks on NVIDIA's long extension string, it needs the AMD card) and any game that gains nothing from the dGPU while keeping it awake. A per-game switch (the `.dgpu` file already exists) would be the place.

## 5. Wayland and the dedicated card
NVIDIA's Wayland EGL/Vulkan platforms do not work while the desktop runs on the integrated GPU under a wlroots-based compositor (the game's window never appears: `eglInitialize` fails). Games must run through X11 (Xwayland): `SDL_VIDEODRIVER=x11` (one value: SDL 1.2 games such as POSTAL 2 reject a list). Fleetwm now sets it itself on machines with an NVIDIA render node; a TuxThrottle-written launch option can carry it too.

## 6. Launch options that only work in some containers
`gamemoderun` inside Steam's runtime container logs `gamemodeauto: dlopen failed - libgamemode.so` for every 32-bit game: harmless, the game still registers with gamemoded through Fleetwm / falcond.

## 7. Things TuxThrottle already does that worked as hoped
Game Mode on/off through the compositor (once per session, not once per launcher window after the 6 s grace), the gate scripts, the per-game `.dgpu` switch, `status --json` for the Settings page readout.
