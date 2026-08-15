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

enum class NavMode {
  Windows,
  Vim,
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
  NavMode nav_mode = NavMode::Windows;
};

// Path helpers. Resolution order: $XDG_CONFIG_HOME/fleetwm/theme.toml (or
// ~/.config/fleetwm/theme.toml), falling back to /etc/xdg/fleetwm/theme.toml
// if the user file doesn't exist yet (first-run default, copied into place
// by the caller on first load).
std::string user_config_path();
std::string system_default_config_path();

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

std::string nav_mode_to_string(NavMode mode);
NavMode nav_mode_from_string(const std::string& s);

}  // namespace fleetwm
