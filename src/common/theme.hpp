#pragma once

#include <string>

namespace fleetwm {

enum class CornerStyle {
  Rounded,
  Sharp,
};

enum class ThemeName {
  Dark,
  Catppuccin,
  Dracula,
  OledBlack,
  Light,
};

struct AccentColor {
  // When auto_extract is true, `hex` holds the last-computed value (see
  // accent_extract.hpp) and is regenerated whenever the wallpaper changes.
  // When false, `hex` is the user's explicit choice from theme.toml.
  bool auto_extract = true;
  std::string hex = "#89b4fa";
};

struct ThemeConfig {
  CornerStyle corner_style = CornerStyle::Rounded;
  ThemeName theme = ThemeName::Dark;
  AccentColor accent;
  // Focus-indicator border thickness in px, drawn by the compositor
  // around whichever window currently has keyboard focus. 2px default
  // matches the previous hardcoded constant it replaces.
  int focus_border_thickness_px = 2;
  // Color of that same focus-only border, independent of `accent` --
  // accent.hex drives UI chrome everywhere (bar, launcher, settings,
  // selection highlights), and reusing it for the focus border too made
  // it hard to tell which window has focus when everything on screen is
  // already the same accent color. Distinct near-white default matches
  // the compositor's own pre-existing hardcoded fallback
  // (kFocusBorderColorFallback in view.cpp) for continuity.
  std::string focus_border_color = "#e6e6f2";
  // Pinned-window (PowerToys-style always-on-top, View::pinned) border
  // color/thickness -- previously hardcoded constants in view.cpp
  // (kPinnedBorderColor/kPinnedFocusedBorderColor/
  // kPinnedBorderThicknessPx), now themeable like the plain focus border
  // above. One shared thickness for both pinned states (matches the
  // previous hardcoded behavior, where both constants already happened
  // to be the same value); the two colors stay distinct so
  // pinned+focused reads as a clearly different state from merely
  // pinned, same as before.
  std::string pinned_border_color = "#3399ff";           // blue
  std::string pinned_focused_border_color = "#99e666";   // green
  int pinned_border_thickness_px = 3;
  // Gap in px between tiled windows (master/stack split, and between
  // stacked windows), applied by Output::relayout(). Does not add extra
  // spacing against the top bar or screen edges -- that's governed
  // separately by the layer-shell exclusive zone (kExclusiveZoneGapPx in
  // output.cpp).
  int gap_px = 2;
};

// Path helpers. Resolution order: $XDG_CONFIG_HOME/fleetwm/theme.toml (or
// ~/.config/fleetwm/theme.toml), falling back to /etc/xdg/fleetwm/theme.toml
// if the user file doesn't exist yet (first-run default, copied into place
// by the caller on first load).
std::string user_config_path();
std::string system_default_config_path();

// Directory containing the installed theme/corner-style CSS files (base.css,
// dark.css, catppuccin.css, corners-rounded.css, accent.css, etc. -- see
// themes/ in the repo root). Resolves to the real installed sysconfdir
// (baked in at compile time via paths_config.h), not a hardcoded guess.
std::string themes_dir();

// Loads theme.toml from the resolved path. Returns the default ThemeConfig{}
// if no config file exists anywhere (fresh install with no packaging step
// having run yet) rather than throwing, so callers never need a fallback
// branch of their own.
ThemeConfig load_theme_config();

// Writes `config` to the user config path, creating parent directories as
// needed. Throws std::runtime_error on I/O failure.
void save_theme_config(const ThemeConfig& config);

// Renders the CSS resource name for a given theme, e.g. ThemeName::Dracula
// -> "dracula.css". Callers combine this with the install-time themes
// directory to build a full path.
std::string theme_css_filename(ThemeName theme);

std::string theme_name_to_string(ThemeName theme);
ThemeName theme_name_from_string(const std::string& s);

// Parses a "#rrggbb" hex color string into normalized [0,1] RGBA floats
// (alpha always 1.0), the format wlr_scene_rect_set_color() expects.
// Returns false (out_rgba left untouched) on malformed input -- callers
// should keep whatever color they already had rather than trust a
// zeroed/garbage result. No support for named colors or an alpha
// channel; theme.toml's accent field is always plain "#rrggbb".
bool parse_hex_color(const std::string& hex, float out_rgba[4]);

}  // namespace fleetwm
