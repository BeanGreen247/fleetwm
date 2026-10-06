# Changelog

All notable changes to Fleetwm. Versions follow `meson.build`; the dated tags are on GitHub.

## 0.3.0 - 2026-10-06

### Added
- Performance section in the README and a 2026-10-06 section in docs/OPTIMIZATIONS.md (measurements, what other
  compositors do, rejected ideas).
- Glass effects setting (Settings -> Theme, `glass_effects` in `theme.toml`): a frosted copy of the
  wallpaper behind translucent surfaces, with a tint, a soft sheen and a light rim. Used by the bar in
  every layout, the taskbar, window titlebars (round glossy buttons), the start menu and Alt+Tab.
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

### Added (tooling)
- `scripts/check-unity-collisions.py` (also runs with `scripts/test.sh` and `meson test --suite static`) and
  `scripts/check-unity.sh` (a real unity build that reports every error, then the unit tests) so a clash like
  that is caught before a tag. Release checklist in `docs/DEVELOPMENT.md`.

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
