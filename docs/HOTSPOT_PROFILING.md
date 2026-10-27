# Profiling Fleetwm with perf and KDAB Hotspot

How the 2026-10-27 profiling round was done, step by step, so it can be repeated on another machine (the remote VM workstation, a gaming PC).
Hotspot is the GUI for `perf record` data: summary, bottom-up, top-down, caller/callee and flame graph tabs, with the time line of samples.

## 1. Install

| Machine | Command | Notes |
|---|---|---|
| Fedora / Nobara (gaming PC) | `sudo dnf install hotspot perf grim gdb` | Hotspot 1.5.1 from the Nobara repo |
| Debian 13 (ThinkPad, VM) | AppImage, no root: `mkdir -p ~/.local/opt/hotspot ~/.local/bin && cd ~/.local/opt/hotspot && curl -LO https://github.com/KDAB/hotspot/releases/download/v1.6.0/hotspot-v1.6.0-x86_64.AppImage && chmod +x hotspot-v1.6.0-x86_64.AppImage && ln -sf $PWD/hotspot-v1.6.0-x86_64.AppImage ~/.local/bin/hotspot` | Debian does not package Hotspot; the AppImage needs `/dev/fuse` (or run it with `--appimage-extract-and-run`). Also `sudo apt install linux-perf` |
| any | `sudo sysctl -w kernel.perf_event_paranoid=1 kernel.kptr_restrict=0` | Lets a normal user record its own processes with kernel symbols. Runtime only; `perf_event_paranoid=3` (Debian default) blocks `perf record` for everyone but root |

Debug symbols: build with `-g` (`meson setup build -Dbuildtype=release -Db_ndebug=false` plus `-Dc_args=-g -Dcpp_args=-g`, or compile a single benchmark with `g++ -O3 -g`).
Release builds keep their symbol tables, which is enough for function names; `-g` adds source lines.

## 2. Record

```
perf record -F 4000 --call-graph dwarf,16384 -o data/step0.data ./bench0 ui        # one program
perf record -F 2500 --call-graph dwarf,8192  -o data/wm0.data -- bash scripts/headless-shot.sh build shot.png PROGRAM   # a whole headless session
```

`--call-graph dwarf` unwinds without frame pointers (libjpeg, zlib, wlroots have none); the 16 KB stack copy makes the file big (20 MB for 0.4 s), so keep runs short.
`perf` follows child processes, so the headless compositor, bar, wallpaper and every helper they spawn are in one file; `perf report --sort comm` splits them.
Text view, handy over SSH and in notes:

```
perf report -i data/step0.data --no-children --stdio -g none --percent-limit 2        # flat profile
perf report -i data/step0.data --no-children --stdio -S build_tree -G                 # who calls one symbol
perf report -i data/wm0.data --no-children --sort comm --stdio -g none                # CPU per process
```

## 3. Look at it in Hotspot, and take the screenshot without touching the live screen

`hotspot data/step0.data` on a desktop. For a screenshot of Hotspot itself the repository's headless rig runs it inside a private Fleetwm compositor (this also proves
the build runs on that machine) and saves the picture with grim:

```
QT_QPA_PLATFORM=wayland SHOT_WAIT=10 scripts/headless-shot.sh build docs/images/hotspot/step0-summary.png hotspot data/step0.data
```

`SHOT_WAIT` has to cover Hotspot's load: about 10 s for a 20 MB file, 40 s for a whole-session file (the screenshot at 10 s showed "Loading Results... 24%").
Hotspot has no command-line switch for a tab; use the tabs by hand for the flame graph, the Summary tab is what the rig captures.

## 4. The rounds, with numbers (gaming PC: Ryzen 7 5800H, 16 threads, Nobara 44, wlroots 0.20.2)

### Round 0: remote desktop encoder (`perflog/2026-10-26/codec-bench.cpp`, "ui" 1280x800 frame)
Baseline `docs/images/hotspot/step0-summary.png`: zlib `build_tree` 19.4 %, `Palette::lookup` 8.1 %, `longest_match_avx2` 6.2 %, `deflate_fast` 6.0 %. Callers of `build_tree`:
10.6 % from `estimate_deflate` (the lossless-versus-JPEG trial), 8.3 % from the real `deflate_into`. Baseline times: Tight jq95 full frame 15.5 ms, 640x400 window 5.8 ms; ZRLE 5.7 ms.

Tried and rejected: Z_FIXED (fixed Huffman trees) for the trial deflate. Time unchanged within noise (16.1 vs 16.4 ms) and the verdicts moved (wallpaper jq95 81.0 KB became 76.9 KB), so the trial
estimate is not a free approximation. Reverted. The tree building is the price of the dynamic trees; the encoder already skips runs in the palette lookups.

### Round 1: session start (compositor + bar + wallpaper in the headless rig, `fleetwm-fm /usr/lib64` requested)
`docs/images/hotspot/wm0-hotspot.png` (before) and `wm1-hotspot.png` (after). CPU cycles by process, same command both times:

| Process | Before (593.6 M cycles in all) | After (314.2 M, repeat 337.3 M) |
|---|---|---|
| `powerprofilesctl` (Python) | 46.6 % | gone |
| fleetwm | 13.5 % | 24.9 % |
| fleetwm-bar | 12.6 % | 21.2 % |
| grim (the screenshot tool itself) | 11.5 % | 22.9 % |
| wpctl | 3.5 % | 6.1 % |

Finding: the bar applied the saved power mode at every start by spawning `powerprofilesctl set`, a Python script whose interpreter start-up cost more CPU than the compositor and bar together.
Fix: a direct D-Bus call from a short-lived thread (`apply_power_mode`, `src/bar/main.cpp`): read `ActiveProfile` first and write it only when it differs
(`org.freedesktop.UPower.PowerProfiles`, falling back to `net.hadess.PowerProfiles`). Session start cost **-47 %** cycles (593.6 M to 314.2 M and 337.3 M on a repeat).
Side effect: no polkit "Not Authorized" error in the log when the profile is already the saved one.

What is left in the compositor and the bar at start-up is dynamic linking: `ld.so` `do_lookup_x` / `_dl_lookup_symbol_x` lead both, then fontconfig. The next candidates
(not tried): prelinked-style `-Wl,-z,nodlopen`/`-Bsymbolic-functions` on the own libraries, fontconfig cache warm-up, and the `wpctl` spawn (3.5 to 6 %) replaced by PipeWire calls the bar already links.

### Round 2: the apps (file manager, Settings, Task Manager, launcher) in the headless rig
Recorded with `perf record` around `scripts/headless-shot.sh` for each program (4 sessions), by process and by symbol:
* **File manager:** the largest own cost was libcrypto hashing (blake2b, sha512, sha256: about 6 % of its samples). Cause: `resolve_algo(Auto)` benchmarked three digests (about 25 ms) at every start, to land on SHA-256 on any CPU that has SHA instructions anyway. Fix: read `sha_ni`/`sha2` from `/proc/cpuinfo` and answer SHA-256 at once; the benchmark stays for CPUs without them (`src/fleetfm/hasher.cpp`, unit-tested flag parser).
* **Task Manager:** `nvidia-smi` was 12.7 % of the whole session's cycles, spawned every third tick. Worse on a hybrid laptop: asking `nvidia-smi` wakes a dedicated GPU that is in runtime suspend, which defeats the integrated-GPU desktop (docs/GAMING.md). Fix: `GpuMonitor::nvidia_all_suspended()` reads `/sys/bus/pci/devices/*/power/runtime_status`; while every NVIDIA display card is suspended the query is skipped and the percentage stays 0 (`src/common/gpu_monitor.cpp`, tested against a fake PCI tree).
* **Settings and launcher:** icon theme lookups (`find_file_in_theme`, stat calls) lead, each under 1 % of the process: no single hot spot.
* **Start-up costs, measured and left alone:** dynamic linking (`do_lookup_x` and friends) was 2.8 % of the compositor's samples and the compositor was a quarter of the session, so under 1 % of the total; fontconfig under 2.5 % of the bar. Not worth a build-flag change. The `wpctl` spawns seen in round 1 are a headless-rig artifact: the bar only polls `wpctl` when it cannot connect to PipeWire, which the rig cannot; a real session connects and never spawns it.
* Other noise in these files is not Fleetwm's: `smbios-battery-*` (TuxThrottle's Dell helper, 28 to 36 % of cycles), `grim` (the screenshot tool), `nvidia-smi`.

### Port finding (not performance): wlroots 0.20
Running the compositor headless on the gaming PC aborted with `wlr_xdg_surface_schedule_configure: Assertion surface->initialized failed`. Cause: `wlr_xdg_toplevel_decoration_v1_set_mode` is called
at `new_toplevel_decoration`, before the toplevel's initial commit, which wlroots 0.19+ forbids. The mode is now sent at the initial commit (`src/compositor/server.cpp`,
`View::pending_decoration`). `wlr_xdg_surface_get_geometry` (removed in 0.19) in `glass_scene.cpp` now uses the `geometry` field behind the same `WLR_VERSION_NUM` guard as `view.cpp`.

## 5. Setting up a workstation or gaming PC

1. Fedora / Nobara: `scripts/install-deps-fedora.sh`, then `meson setup build -Dbuildtype=release && ninja -C build` and `scripts/run-tests.sh` (1269 tests pass on Nobara 44).
   `install.sh` itself is Debian-only: it also installs the greeter and swaps display managers, which is not wanted on a KDE gaming PC. Run the programs from `build/` in a nested session instead.
2. `sudo sysctl -w kernel.perf_event_paranoid=1 kernel.kptr_restrict=0` (add a `/etc/sysctl.d/` file to keep it).
3. `scripts/setup-hotspot.sh` does section 1 for you on Debian/Ubuntu and Fedora/Nobara (perf, Hotspot, the sysctl). The VM workstation (192.168.0.223) did not answer from here ("No route to host"), so it was not set up there; run the script on it. Then Hotspot as in section 1, record, screenshot with the headless rig, note the numbers next to the screenshot.
4. Hybrid-GPU machines (AMD iGPU + NVIDIA dGPU, like the gaming PC: `card1=amdgpu card2=nvidia`): the headless rig always uses pixman; the real GPU renderer was not measured on it yet.
   Measure the GLES path from a real session on the AMD iGPU first, then the NVIDIA card, and record both.

## 6. Taskbar colours

`~/.config/fleetwm/bar.toml`, every entry optional, empty = follow the theme, `#rrggbb` or `#rrggbbaa`:

```toml
[taskbar_colors]
bg = "#1b2a41"        # taskbar / bar surface (the glass tint with glass on)
start = "#ff7800"     # start button
active = "#22cc88"    # active window button fill and the running-program lines
clock_fg = "#ffd166"  # the time
status_fg = "#7bdff2" # status icons (volume, network, Bluetooth, power mode, battery) and the date

[workspace_colors]    # unchanged: the workspace buttons
active_bg = "#22cc88"
```

Screenshots: `docs/images/hotspot/taskbar-default.png`, `taskbar-colored.png` (headless rig, the file above).
