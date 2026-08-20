#include "keybinds_config.hpp"

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

std::string keybinds_user_config_path() {
  return (config_home() / "fleetwm" / "keybinds.toml").string();
}

std::string keybinds_system_default_config_path() {
  return std::string(FLEETWM_SYSCONF_DIR) + "/keybinds.toml";
}

KeybindsConfig load_keybinds_config() {
  KeybindsConfig config;

  fs::path path = keybinds_user_config_path();
  if (!fs::exists(path)) {
    path = keybinds_system_default_config_path();
    if (!fs::exists(path)) {
      return config;
    }
  }

  toml::table table = toml::parse_file(path.string());
  if (auto v = table["terminal"].value<std::string>()) {
    config.terminal = *v;
  }
  if (auto v = table["launcher"].value<std::string>()) {
    config.launcher = *v;
  }
  if (auto v = table["close_window"].value<std::string>()) {
    config.close_window = *v;
  }
  if (auto v = table["toggle_pin"].value<std::string>()) {
    config.toggle_pin = *v;
  }
  if (auto v = table["toggle_float"].value<std::string>()) {
    config.toggle_float = *v;
  }
  if (auto v = table["lock"].value<std::string>()) {
    config.lock = *v;
  }
  if (auto v = table["screenshot"].value<std::string>()) {
    config.screenshot = *v;
  }
  if (auto v = table["focus_left"].value<std::string>()) {
    config.focus_left = *v;
  }
  if (auto v = table["focus_down"].value<std::string>()) {
    config.focus_down = *v;
  }
  if (auto v = table["focus_up"].value<std::string>()) {
    config.focus_up = *v;
  }
  if (auto v = table["focus_right"].value<std::string>()) {
    config.focus_right = *v;
  }
  if (auto v = table["quit"].value<std::string>()) {
    config.quit = *v;
  }
  if (auto v = table["debug_overlay"].value<std::string>()) {
    config.debug_overlay = *v;
  }

  return config;
}

void save_keybinds_config(const KeybindsConfig& config) {
  fs::path path = keybinds_user_config_path();
  fs::create_directories(path.parent_path());

  toml::table table;
  table.insert_or_assign("terminal", config.terminal);
  table.insert_or_assign("launcher", config.launcher);
  table.insert_or_assign("close_window", config.close_window);
  table.insert_or_assign("toggle_pin", config.toggle_pin);
  table.insert_or_assign("toggle_float", config.toggle_float);
  table.insert_or_assign("lock", config.lock);
  table.insert_or_assign("screenshot", config.screenshot);
  table.insert_or_assign("focus_left", config.focus_left);
  table.insert_or_assign("focus_down", config.focus_down);
  table.insert_or_assign("focus_up", config.focus_up);
  table.insert_or_assign("focus_right", config.focus_right);
  table.insert_or_assign("quit", config.quit);
  table.insert_or_assign("debug_overlay", config.debug_overlay);

  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("failed to open " + path.string() + " for writing");
  }
  out << table << "\n";
}

}  // namespace fleetwm
