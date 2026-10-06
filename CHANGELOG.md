# Changelog

All notable changes to Fleetwm. Versions follow `meson.build`; the dated tags are on GitHub.

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
