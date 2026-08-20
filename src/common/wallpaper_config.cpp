#include "wallpaper_config.hpp"

#include <toml++/toml.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "config_paths.hpp"
#include "paths_config.h"

namespace fleetwm {

namespace fs = std::filesystem;
using config_internal::config_home;

std::string wallpaper_user_config_path() {
  return (config_home() / "fleetwm" / "wallpaper.toml").string();
}

std::string wallpaper_system_default_config_path() {
  return std::string(FLEETWM_SYSCONF_DIR) + "/wallpaper.toml";
}

WallpaperConfig load_wallpaper_config() {
  WallpaperConfig config;

  fs::path path = wallpaper_user_config_path();
  if (!fs::exists(path)) {
    path = wallpaper_system_default_config_path();
    if (!fs::exists(path)) {
      return config;
    }
  }

  toml::table table = toml::parse_file(path.string());
  if (auto v = table["path"].value<std::string>()) {
    config.path = *v;
  }
  if (auto v = table["use_solid_color"].value<bool>()) {
    config.use_solid_color = *v;
  }
  if (auto v = table["solid_color"].value<std::string>()) {
    config.solid_color = *v;
  }

  return config;
}

void save_wallpaper_config(const WallpaperConfig& config) {
  fs::path path = wallpaper_user_config_path();
  fs::create_directories(path.parent_path());

  toml::table table;
  table.insert_or_assign("path", config.path);
  table.insert_or_assign("use_solid_color", config.use_solid_color);
  table.insert_or_assign("solid_color", config.solid_color);

  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("failed to open " + path.string() + " for writing");
  }
  out << table << "\n";
}

}  // namespace fleetwm
