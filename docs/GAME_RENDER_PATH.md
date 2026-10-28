# Game render path: design for the lowest latency at the hardware's peak

Goal: while a game is fullscreen, the compositor does the least possible between the game's frame and the screen, nothing else competes with the game, and input and audio latency stay as low as the hardware allows. Everything here is checked with `fleetwm-ctl frame-stats` (what the screen really got: interval percentiles, late and hitched frames, the compositor's commit cost).

## What the measurements say (Ryzen 7 5800H + RTX 3050 Ti, desktop on the AMD iGPU, 144 Hz panel)
* Normal games through Xwayland on the NVIDIA card: 143.9 fps presented, p99 6.95 ms, 0 hitches (Half-Life 2 family, Portal, TF2, DiRT Showdown, POSTAL 2, OpenTTD, CS: Source).
* The compositor's own work per frame is about 0.3 ms. The desktop behind a fullscreen window is already idle: the taskbar, wallpaper and other windows are hidden and get no frame callbacks (checked: nothing was left to skip).
* An unlimited game on the dedicated card (osu! `FrameSync = Unlimited`, NVIDIA at 99 %): 2 frames per second presented, p99 580 ms, 17-19 hitched frames per 10 s, while the compositor's commit stays at 0.3 ms with an occasional 570 ms blocking commit. The time is spent waiting for the game's buffer, not drawing. With `Limit4x`: 144 fps, p99 7.0 ms, 0 hitches.

## The path, step by step
1. **Direct scanout** (wlroots): the fullscreen window's buffer goes to the display without composing. Least latency. Setting `direct_scanout` in game.toml: `on` (default), `auto` (off only for a window whose program was started for the dedicated card: `__NV_PRIME_RENDER_OFFLOAD=1` or `DRI_PRIME=1` in its environment) or `off`; `fleetwm-ctl scanout on|off|auto` switches it live for a comparison. Hypothesis to test: for a card-rendered game the display flip waits on the other GPU's queue, composing instead lets the compositor keep presenting.
2. **Never wait for the newest frame**: present the newest buffer that is already finished (explicit sync, `wp_linux_drm_syncobj`, already offered; Xwayland has to use it). Not yet verified for the NVIDIA/X11 path.
3. **Tearing and variable refresh** for a fullscreen game (`wp_tearing_control`, `adaptive_sync`): no waiting for the refresh when the game asks for it. Already in.
4. **Late render start** (`max_render_time_ms`) so a fullscreen picture is drawn as late as it can be: newest input on screen. Already in, off by default.
5. **Nothing else competes**: the desktop is idle behind the game (above); GameMode and TuxThrottle's profile start with the first game window and end 6 s after the last; the compositor can run with real-time priority (`realtime`, rtkit).
6. **Input**: pointer lock and relative motion, shortcut inhibit (already in); raw acceleration-free mouse through the KWin bridge (`pointerAccelerationProfileFlat`, so the osu! launcher can switch acceleration off).
7. **Audio**: not the compositor's job, but its choices matter: no real-time hog, no stalls. PipeWire quantum is set by the game's launcher (osu: 256 frames, about 5 ms); TuxThrottle could pin it per game.
8. **Cap the game**: the one lever that was proven: a game drawing thousands of frames a second on the dedicated card saturates it and delays the frame the screen needs. Cap at about twice the refresh rate (`fleetwm-game --fps`, MangoHud `fps_limit`, osu! `Limit4x`, NVIDIA `__GL_...`): same latency, no stall. A per-game automatic cap belongs in TuxThrottle's launch options (`docs/TUXTHROTTLE_NOTES.md`).

## Test protocol after the next login
1. `fleetwm-ctl frame-stats reset`, start osu! with `FrameSync = Unlimited`, `fleetwm-ctl frame-stats` after 20 s: expect about 2 fps, p99 580 ms (the bad case).
2. `fleetwm-ctl scanout off`, reset, wait 20 s, read again. If p99 drops to about 7 ms, direct scanout is the culprit for card-rendered games and `direct_scanout = "auto"` becomes the default. If not, the fence is in the buffer's source (Xwayland's blit) and only a cap fixes it.
3. Same for a normal game (HL2) with scanout on and off to measure what direct scanout buys in input latency (use the late-render stats and a photodiode-free proxy: commit-to-present).
