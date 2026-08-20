#include "default_apps.hpp"

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

std::string default_apps_user_config_path() {
  return (config_home() / "fleetwm" / "default_apps.toml").string();
}

std::string default_apps_system_default_config_path() {
  return std::string(FLEETWM_SYSCONF_DIR) + "/default_apps.toml";
}

DefaultAppsConfig load_default_apps_config() {
  DefaultAppsConfig config;

  fs::path path = default_apps_user_config_path();
  if (!fs::exists(path)) {
    path = default_apps_system_default_config_path();
    if (!fs::exists(path)) {
      return config;
    }
  }

  toml::table table = toml::parse_file(path.string());
  if (auto v = table["terminal_command"].value<std::string>()) {
    config.terminal_command = *v;
  }

  return config;
}

void save_default_apps_config(const DefaultAppsConfig& config) {
  fs::path path = default_apps_user_config_path();
  fs::create_directories(path.parent_path());

  toml::table table;
  table.insert_or_assign("terminal_command", config.terminal_command);

  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("failed to open " + path.string() + " for writing");
  }
  out << table << "\n";
}

}  // namespace fleetwm
