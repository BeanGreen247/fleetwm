# Changelog

All notable changes to Fleetwm. Versions follow `meson.build`; the dated tags are on GitHub.

## Unreleased

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
- Windows 7 style window frame in the Desktop layout: windows with a titlebar get a frame (6 px by default, `frame_px`
  under `[titlebar]` in `theme.toml`, 0 turns it off) whose glass runs unbroken from the titlebar down both sides
  and along the bottom. It follows Glass effects: translucent glass with a light outer rim, or flat and opaque. Maximized
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
