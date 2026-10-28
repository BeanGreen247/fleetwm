# Gaming and low-latency use

What the compositor does for games, what is switched on by itself, what is configured, and what is still open. Status words: **built** = compiles and unit tests pass on wlroots 0.18 and 0.20;
**checked in the headless rig** = `scripts/game-input-test.sh` ran it against small test clients on a private compositor (virtual pointer, no GPU); **not run on hardware** = no game, GPU or monitor exercised it yet.

## In the compositor

| Feature | Status | Notes |
|---|---|---|
| Pointer lock and confine (`pointer-constraints`, `relative-pointer`) | lock checked in the headless rig (lock granted, relative events arrive, absolute motion ignored while locked); confine and real games not run | `src/compositor/game_support.cpp`. First-person aiming, SDL relative mode, Proton through Xwayland. A locked pointer stays put and the game reads raw movement; the cursor hint puts the pointer back where the game says on release |
| Keyboard shortcut inhibit | built, not run on hardware | A fullscreen game that asks gets Alt+Tab, Super and every other key. **Super+Pause** hands the shortcuts back to the compositor until the window is focused again |
| Aspect-correct fullscreen scaling | checked in the headless rig: a 640x480 fullscreen client on a 1280x720 output, all four modes (`docs/images/gaming/fit-*.png`); maths unit-tested; no real game | `game.toml`, below. Old 4:3 games, emulators and anything that does not fill the screen get black bars, whole-number zoom or a stretch instead of sitting in a corner |
| Variable refresh rate | built, not run on hardware | On while a window is fullscreen and the monitor reports support, off again afterwards. `adaptive_sync = false` in `game.toml` disables it |
| Tearing (async page flips) | existing | A fullscreen surface that sends the `tearing-control` hint is presented immediately |
| Direct scanout ("exclusive fullscreen") | existing | A fullscreen dmabuf client at the screen's size is shown straight from its own buffer, no composition (needs the linux-dmabuf feedback the scene sends). Scaled output (the aspect fix) is composited, so a game at the screen's native size stays on the fast path |
| Frame timing (`presentation-time`), per-surface scale (`fractional-scale`), explicit sync (`linux-drm-syncobj`) | built, not run on hardware | Offered as globals; the wlroots scene sends the feedback, the scale and the fences. Explicit sync is offered only on wlroots 0.19+ with a DRM device that supports timelines (the NVIDIA proprietary driver needs it for flicker-free presentation) |
| Mouse | existing | Flat acceleration profile unless "Enhance pointer precision" is on in Settings |
| Compositor priority | existing | nice -10 |

| GameMode for every game | checked in the headless rig (a `steam_app_` window is registered with gamemoded and released when it closes) | `evaluate_game()`: Steam/Proton (`steam_app_*`, `SteamGameId` in the environment), Wine (`.exe`, `WINEPREFIX`), Lutris, gamescope, anything in `gamemode_apps`, and any other fullscreen window except video players and browsers. gamemoded then sets the governor, priorities and GPU clocks (it needs polkit rights for the governor; without them it still counts the game) |
| Dedicated vs integrated GPU | order and log checked on the gaming PC, no real DRM session | see "Which graphics card" |
| Startup apps | checked in the headless rig (an enabled XDG autostart entry runs, a `Hidden` one does not) | Settings -> Startup Apps, below |

| Late render start (`max_render_time_ms`) | built; the rig run only shows it does not disturb a fullscreen game | The frame is drawn that many ms before the next refresh (from the output's last presentation time), only for a fullscreen game, not with VRR or tearing |
| Real-time priority (`realtime`) | built, **not verified**: the gaming PC's ssh shell runs `SCHED_IDLE` and rtkit answered "Operation not permitted" there | rtkit `MakeThreadRealtime` for the main thread while a game runs, `RLIMIT_RTTIME` 200 ms as the safety net, back to `SCHED_OTHER` after; off by default |
| Game mode switching (`mode_switch`) | built, **not verified**: the headless output has no mode list | The monitor goes to the mode that is exactly the fullscreen picture's size (only a mode it lists) and back when the window leaves or closes; not written to `outputs.toml`; off by default |
| `wp_content_type` | built, not run with a client that sends it | A surface that says `game` counts as a game (GameMode, TuxThrottle) even windowed; `video` and `photo` never do |

## `~/.config/fleetwm/game.toml`

```toml
fullscreen_scaling = "fit"      # fit (default) | integer | stretch | off
fullscreen_filter  = "auto"     # auto (nearest for integer zoom, smooth otherwise) | nearest | linear
adaptive_sync      = true       # VRR while a window is fullscreen
max_render_time_ms = 0          # late render start, 0 = off, try 4 to 6
realtime           = false      # rtkit real-time class while a game runs
mode_switch        = false      # switch the monitor to the game's own resolution when it is one of the monitor's modes
desktop_gpu        = "integrated" # integrated (default) | discrete | default; applies at the next login
gamemode           = true       # GameMode for every game (see the table)
gamemode_apps      = ["osu"]    # extra app ids that count as games
fps_limit          = 0          # frame cap for fleetwm-game / libstrangle, 0 = none
```
All of it is on Settings -> Gaming.

Picked up while the compositor runs. `integer` is the pixel-art / DOS setting: a 320x200 game on 1920x1080 is drawn at 5x (1600x1000) with bars. `stretch` fills the screen like a TV's "zoom".
For a game that needs a different *render* resolution as well, gamescope does the same job from the other side: `gamescope -w 640 -h 480 -W 1920 -H 1080 -S fit -- game`
(`-S integer` and `-S stretch` exist; `-F nearest|fsr|nis` picks the filter).

### "Exclusive fullscreen"
Wayland has no exclusive mode: the compositor decides. The equivalent here is direct scanout plus tearing plus VRR, all of which switch on by themselves for a fullscreen game (table above).
A true mode switch (changing the monitor's mode to the game's resolution, as X11 `xrandr` games did) is **not implemented**: it would relayout every window on the output and was not worth the risk untested.
Use the scaling modes above, or run the game inside gamescope at the resolution it wants.

## Which graphics card (integrated for the desktop, dedicated on request)

How the others do it: GNOME and KDE treat the boot VGA device (the integrated GPU on a hybrid machine) as the default card, draw the desktop with it and offer "Launch using Discrete Graphics Card" for single programs
(`switcherooctl launch`, from switcheroo-control, which knows each vendor's offload variables; the desktop-entry key `PrefersNonDefaultGPU=true` asks for it). Steam users add `prime-run %command%` or `DRI_PRIME=1 %command%`;
the NVIDIA proprietary driver uses `__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia __VK_LAYER_NV_optimus=NVIDIA_only`; gamescope picks a Vulkan card with `--prefer-vk-device`; sway/wlroots set `WLR_DRM_DEVICES`
with the card to render with first. The game's frames come back as dmabufs and the desktop card shows them. Fleetwm does the same:

* **Found on the real gaming laptop (Nobara, Ryzen 7 5800H + RTX 3050 Ti), first login:** listing both cards made wlroots open the NVIDIA card too and start a second OpenGL renderer on it: the compositor's memory was 121 MB (PSS) instead of 61 MB (measured in the headless rig with the renderer on the AMD node versus the NVIDIA node: 61 MB / 35 MB anon against 121 MB / 79 MB anon), and the dedicated GPU sat awake at 17 W with the compositor holding `/dev/nvidia0`. Fix: with `integrated`, a dedicated card with no screen connected is left out of `WLR_DRM_DEVICES` altogether (`DrmCard.has_display`, read from the `cardN-*/status` files). A screen plugged into the dedicated card's port (the laptop's HDMI) after login is only picked up at the next login; use `desktop_gpu = "discrete"` if that port is your daily screen.
* **Desktop:** at start-up the compositor puts the integrated card first in `WLR_DRM_DEVICES` (`desktop_gpu = "integrated"`; "discrete" reverses it, "default" leaves wlroots alone; an explicit `WLR_DRM_DEVICES` in the environment wins). Every card stays listed, so
  screens on the dedicated card (a laptop's HDMI port is often wired to it) keep working. "Integrated" is the boot VGA device, never an NVIDIA card (`src/common/gpu_policy.cpp`, unit-tested). On the gaming PC the order logged is `card1 (amdgpu):card2 (nvidia)`.
* **One program:** `fleetwm-game --gpu discrete -- COMMAND` (Steam launch options: `fleetwm-game --gpu discrete -- %command%`) uses switcherooctl when present (the gaming PC: `switcherooctl launch -g 1`), else `DRI_PRIME=1` plus the NVIDIA variables when that driver is loaded.
  A desktop entry with `PrefersNonDefaultGPU=true` or `X-KDE-RunOnDiscreteGpu=true` is started that way by the launcher.
* **Not done / not verified:** a real DRM session with both cards; whether the dedicated card really sleeps while the desktop runs on the integrated one (check `cat /sys/class/drm/card2/device/power/runtime_status` and `nvidia-smi`); NVIDIA proprietary
  buffer sharing to the AMD card with the 570-series drivers; per-game automatic offload (a game is only offloaded when started through `fleetwm-game` or a desktop entry that asks).

## `fleetwm-game` (GameMode, frame cap, GPU)

`fleetwm-game [--gpu discrete|integrated] [--fps N] [--no-gamemode] [--dry-run] -- COMMAND`: prefixes `gamemoderun` (GameMode from the very first frame, before the compositor has even seen a window) and `strangle N` (libstrangle's frame cap; not packaged on Debian or Fedora,
build it from github.com/torkel104/libstrangle; installed in `/usr/local/bin` on the gaming PC). Defaults come from `game.toml`. `--dry-run` prints the command, e.g. `switcherooctl launch -g 1 gamemoderun strangle 90 echo hi`.
Installed with the other binaries by `meson install`.

## TuxThrottle

TuxThrottle (the gaming-laptop tool: power profile, fans, TDP, Game Mode; `~/tuxthrottle` on the gaming PC, a Dell G15) is used through its CLI, `tuxthrottlectl`; nothing happens when it is not installed (`src/common/tuxthrottle.cpp`).

* **Game Mode follows the games.** The first game the compositor registers with GameMode also runs `tuxthrottlectl gamemode on` (TuxThrottle's own helper scripts: performance profile, fan boost, GPU clocks, through the narrow sudo rule it installs); after the last game closes it runs `gamemode off`.
  `tuxthrottle = false` in `game.toml` (Settings -> Gaming, "TuxThrottle Game Mode") turns that off. Checked in the headless rig with a stand-in `tuxthrottlectl` (on, then off); the real `gamemode on` was not run on the gaming PC.
* **Readout in Settings -> Gaming:** model, power profile, processor temperature and power, dedicated GPU awake/asleep with temperature and power, Game Mode state, a Refresh button and a "Turn Game Mode on now" button. `tuxthrottlectl status --json` needs no privileges (0.3 s); the parser is unit-tested against that command's real output on the gaming PC.
* **Two routes, one result.** On the gaming PC `/etc/gamemode.ini` already runs `gaming-performance` / `gaming-balanced` from gamemoded's start/end hooks (TuxThrottle's GameModeBridge tweak), so GameMode alone triggers them too; the page says which case it is. gamemoded runs those hooks as the user, not through sudo,
  so the fan boost in `gaming-performance` may not take effect that way; the direct `tuxthrottlectl gamemode on` call uses the sudo rule and does.
* **Bar:** the Metrics element's CPU and GPU tooltips carry TuxThrottle's lines (profile, Game Mode, processor temperature; dedicated GPU awake or asleep) when `tuxthrottlectl` is there; read on a background thread every few ticks.
* Not done: TuxThrottle's hybrid-graphics mode (`gpumode`, EnvyControl) is left to TuxThrottle; Fleetwm's `desktop_gpu` is a per-session choice that works without it.

## Startup apps

Settings -> Startup Apps lists the installed applications with a switch each. On writes a standard XDG autostart entry to `~/.config/autostart/` (marked `X-Fleetwm-Autostart=true`); off deletes that file. An entry written by something else is never deleted: off adds `Hidden=true`, on removes it again.
The compositor runs the enabled entries (via `/bin/sh -c`, field codes dropped) after its own helpers at start-up. Only the user's own `~/.config/autostart` is read, not `/etc/xdg/autostart`, which carries other desktops' helpers.

## Opt-in system tuning

`sudo scripts/gaming-tune.sh --dry-run` shows the changes, `sudo scripts/gaming-tune.sh` applies them until reboot, `--undo` restores the recorded values. It touches only runtime sysfs/sysctl values:
CPU governor and energy preference to `performance`, `kernel.split_lock_mitigate=0`, `vm.max_map_count=2147483642`, THP `madvise`, NVMe scheduler `none`, `sched_autogroup` off.
On the gaming PC (Ryzen 7 5800H, Nobara 44) the dry run found governor `powersave` and EPP `balance_performance` on all 16 threads, autogroup on and THP `always`; the other values were already right. Not applied there.

Other things that are well known to matter and live outside the compositor (not done, not measured here): `gamemoderun %command%` (installed on the gaming PC), `mangohud` for frame times,
`PROTON_ENABLE_WAYLAND=1` where the Proton build has it, NVIDIA `nvidia-drm.modeset=1`, `usbhid.mousepoll=1` for a 1000 Hz mouse, and on hybrid laptops running the game on the dGPU (`prime-run` / `DRI_PRIME=1`).
Kernel boot options such as `mitigations=off` or isolating cores trade security or desktop smoothness for speed; they are mentioned only so nobody expects them from this repository.

**Headless and nested sessions never boost the machine.** GameMode, TuxThrottle and real-time priority are skipped when `WLR_BACKENDS` is set to something without `drm` (the test rigs, PGO training, a window in another desktop): they have no screen of their own to speed up. The game test rig sets `FLEETWM_GAMEMODE_TEST=1` and a private `gamemode.ini`, so it never runs the machine's own start/end scripts.
(Found the hard way: a first rig run registered fake games with a gamemoded that ran `/etc/gamemode.ini`'s TuxThrottle hooks, which left the gaming PC in performance mode with the fans at 255 until `tuxthrottlectl gamemode off`.)
The real `tuxthrottlectl gamemode on` and `off` were run once on the gaming PC: `on` returned 0, `off` returned the profile to balanced and the fan boost to 0.

## Reproduce the checks

`scripts/game-input-test.sh build /tmp/out` builds the test clients (`perflog/2026-10-27/game-clients/`) and `scripts/pgo-pointer.c` (its new `rel DX DY` command sends mouse-like relative motion), runs them in a private headless
session and prints PASS/FAIL for the pointer lock, plus a screenshot per scaling mode. `fit-fit.png` (960x720 with bars, square checks), `fit-integer.png` (640x480, 1x), `fit-stretch.png`, `fit-off.png`.
A locked pointer also ignores absolute positions (tablets, remote-desktop input), so a game under a locked pointer gets no input from those.

## Taskbar and desktop while gaming

* **Power mode gauge:** follows power-profiles-daemon's `ActiveProfile` live (a system-bus signal), not the mode saved in `bar.toml`; TuxThrottle's Game Mode and GameMode hooks change that profile behind the bar's back.
* **Separate GPUs:** the taskbar's metrics grid shows each GPU apart when there are two or more ("AMD 3%", "NVIDIA 0%"; "GPU1/GPU2" when the vendors match); the Task Manager already has a graph and block per GPU.
* **Tray:** icons are 22 px (`tray_icon_size`, 16 to 32). StatusNotifierItem programs show as before. Wine and Proton programs (the Rockstar launcher, Battle.net, Steam's old icon) use the old XEmbed tray, and the bar now has one built in (`src/bar/xembed_tray.cpp`, xcb plus XComposite): once Xwayland is running (the bar never starts it; it checks for the `Xwayland` process serving `$DISPLAY` every 1.5 s) it takes the `_NET_SYSTEM_TRAY_S0` selection, parks each docked icon window outside every output, copies its picture four times a second, and draws it next to the other icons. Clicks go to the icon window as X button events (left, middle, right). Checked in the rig with a fake docked icon (`perflog/2026-10-27/game-clients/xtray.c`: it appears in the bar and a click reaches it). Icons keep their transparency: the bar publishes a 32-bit ARGB visual (`_NET_SYSTEM_TRAY_VISUAL`, which Wine looks for) and the icon containers are transparent ARGB windows; programs that only have a plain visual are drawn opaque. Limits: a program's own popup menu opens where the program puts it, which can be far from the bar; icons that hide by unmapping their window stay as a blank square. KDE's `xembedsniproxy` was tried first and rejected: it makes every X11 window stop reaching the compositor under Fleetwm's rootless Xwayland. Not tested with the real Rockstar launcher yet.
  Steam's own tray item is a StatusNotifierItem built on libappindicator: it has no `Activate` or `ContextMenu`, only a `Menu` (com.canonical.dbusmenu). A click with either button now reads that menu (`AboutToShow`, `GetLayout`) and shows it in the taskbar's menu window; choosing an entry sends `Event "clicked"` back through `busctl`. Checked in the rig with a fake item (`perflog/2026-10-27/game-clients/sni_menu.py`); not yet with Steam itself.
* **Colours:** Settings -> Taskbar has Colours (background, start button, active window button, clock, status icons, remote desktop pill, keyboard layout pill, workspace buttons).
* **Glass stays on** while gaming by default. `fast_desktop = true` (Settings -> Gaming, "Drop glass while gaming") drops the glass for the compositor and the bar while a game runs or the profile is Performance; checked in the rig (taskbar 27 colours during, 148 after).

## Found on the gaming laptop, second round

* **Borderless X11 games over the taskbar:** a Wine/Proton game that makes an undecorated window as big as the screen, instead of asking for fullscreen, sat under the taskbar's layer so the bar was drawn over the bottom of the picture. Such a window (X11, covers the whole output, undecorated or placed at the output's corner) is now treated as fullscreen, so the bar is hidden; it goes back to a normal window when the game shrinks it. Checked in the rig with an X11 test window the size of the screen: the old build draws the taskbar over it (FAIL), the new one does not (`scripts/game-input-test.sh`).
* **Task Manager, Performance tab:** one sidebar entry per GPU, "GPU 0" (AMD) and "GPU 1" (NVIDIA), each with its own graph and figures, instead of one combined entry. `fleetwm-taskmgr --tab performance --resource gpu1` opens one directly.
* **Touchpad settings from TuxThrottle:** TuxThrottle switches the touchpad (on/off, tap-to-click, natural scroll, disable while typing) through KWin's D-Bus interface, which does not exist under Fleetwm, so nothing it set had any effect. The compositor now serves `org.kde.KWin.InputDevice` (`/org/kde/KWin/InputDevice/eventN`, properties `touchpad`, `enabled`, `tapToClick`, `naturalScroll`, `disableWhileTyping`, `leftHanded`) on the session bus and applies writes to libinput; a value set from outside is kept for the session and re-applied after Settings -> Mouse applies its own, so TuxThrottle's choice is never silently overwritten. Built and compiled; **not run against a real touchpad** (the headless rig has no libinput devices), so check it on the laptop with `tuxthrottle`'s Touchpad tab or `busctl --user tree org.kde.KWin`.
* **Tray:** the Rockstar launcher's icon goes through the bar's own XEmbed tray; see the Tray bullet above.

## Install on Fedora / Nobara

`./install-fedora.sh` (env: `FLEETWM_PREFIX`, `FLEETWM_SKIP_DEPS`, `FLEETWM_RUNTIME`, `FLEETWM_TESTS`, `FLEETWM_PGO=0` to skip the profile-guided build, which runs by default like on Debian): dnf build dependencies, optional run-time programs, release build, `ninja install`, the "Fleetwm" login session entry, video/render groups. It does not touch the display manager or install the greeter.
Run on the gaming PC into `~/fleetwm-prefix` (no sudo): all 24 programs and the session file installed. (It also added the user to the `video` and `render` groups there.) PGO (the default; into `~/fleetwm-prefix-pgo` when tried) ran on the gaming PC: instrumented build 118 s, training 93 s, optimized build 95 s, 5 min 6 s in all with 1298 tests passing, and `scripts/game-input-test.sh` passes against that build. The installer also installs the lock screen's PAM service (`fleetwm-locker`), which was missing.
The login screen is opt-in and experimental there: `FLEETWM_GREETER=1` installs its PAM service, systemd unit and seatd but leaves it disabled; `FLEETWM_GREETER=enable` also switches the console over and disables the display manager for the next boot (never stops it). The greeter was written from the Debian one and has not been booted on Fedora (SELinux and SDDM interplay untested).

## Open

0. Verify what is marked "not verified" above on a normal login session (real-time priority needs a session that is not `SCHED_IDLE`; mode switching needs a real monitor).
1. Run real games: a first-person title through Xwayland (pointer lock), a Vulkan title (direct scanout, tearing, presentation-time), a 4:3 title (scaling), on the AMD iGPU and the NVIDIA card. Record frame times next to the Hotspot data (`docs/HOTSPOT_PROFILING.md`).
2. Measure what late render start really saves (input-to-photon), and pick its default.
