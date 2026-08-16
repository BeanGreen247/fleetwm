#pragma once

#include <string>

namespace fleetwm {

struct WallpaperConfig {
  // Absolute path to the wallpaper image, or empty for "no image set
  // yet" (fleetwm-wallpaper falls back to a solid bg_primary-colored
  // background in that case, same as any other themed panel, rather
  // than erroring).
  std::string path;
  // When true, use `solid_color` as a flat background fill instead of
  // rendering `path`'s image -- independent toggle rather than just
  // "clear path", so a previously-chosen image isn't lost when
  // switching to a solid color and back.
  bool use_solid_color = false;
  std::string solid_color = "#1e1e2e";  // matches dark.css's bg_primary default
};

// Path helpers, mirroring theme.hpp/bar_config.hpp's own
// user_config_path()/system_default_config_path() pair but for
// wallpaper.toml instead.
std::string wallpaper_user_config_path();
std::string wallpaper_system_default_config_path();

// Loads wallpaper.toml. Returns WallpaperConfig{} (empty path) if no
// config file exists anywhere yet, same fresh-install contract as
// load_theme_config()/load_bar_config().
WallpaperConfig load_wallpaper_config();

// Writes `config` to the user config path, creating parent directories
// as needed. Throws std::runtime_error on I/O failure.
void save_wallpaper_config(const WallpaperConfig& config);

}  // namespace fleetwm
