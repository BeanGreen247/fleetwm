#include "keybinds_config.hpp"

#include <toml++/toml.h>

#include <cctype>

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

unsigned modifier_mask(const std::string& names) {
  unsigned mask = 0;
  size_t pos = 0;
  while (pos <= names.size()) {
    size_t end = names.find('+', pos);
    if (end == std::string::npos) end = names.size();
    std::string n = names.substr(pos, end - pos);
    const size_t first = n.find_first_not_of(" \t");
    n = first == std::string::npos ? std::string() : n.substr(first, n.find_last_not_of(" \t") - first + 1);
    for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (n == "super" || n == "logo" || n == "win" || n == "meta") mask |= kModLogo;
    else if (n == "alt") mask |= kModAlt;
    else if (n == "ctrl" || n == "control") mask |= kModCtrl;
    else if (n == "shift") mask |= kModShift;
    else if (!n.empty() || names.empty()) return 0;  // unknown name (or nothing at all)
    pos = end + 1;
  }
  return mask;
}

KeyCombo parse_key_combo(const std::string& combo) {
  KeyCombo out;
  const size_t last = combo.rfind('+');
  const std::string mods = last == std::string::npos ? "" : combo.substr(0, last);
  std::string key = last == std::string::npos ? combo : combo.substr(last + 1);
  const size_t a = key.find_first_not_of(" \t"), b = key.find_last_not_of(" \t");
  key = a == std::string::npos ? std::string() : key.substr(a, b - a + 1);
  if (key.empty()) return out;
  if (!mods.empty()) {
    out.mods = modifier_mask(mods);
    if (out.mods == 0) return out;  // unknown modifier name
  }
  out.key = key;
  out.valid = true;
  return out;
}

bool combo_mods_match(unsigned held, unsigned wanted) {
  constexpr unsigned kCompared = kModShift | kModCtrl | kModAlt | kModLogo;
  return (held & kCompared) == (wanted & kCompared);
}

std::vector<std::string> split_key_names(const std::string& list) {
  std::vector<std::string> out;
  size_t pos = 0;
  while (pos <= list.size()) {
    size_t end = list.find(',', pos);
    if (end == std::string::npos) end = list.size();
    std::string n = list.substr(pos, end - pos);
    const size_t a = n.find_first_not_of(" \t");
    const size_t b = n.find_last_not_of(" \t");
    if (a != std::string::npos) out.push_back(n.substr(a, b - a + 1));
    pos = end + 1;
  }
  return out;
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
  if (auto v = table["start_menu_key"].value<std::string>()) config.start_menu_key = *v;
  if (auto v = table["desktop_terminal"].value<std::string>()) config.desktop_terminal = *v;
  if (auto v = table["desktop_browser"].value<std::string>()) config.desktop_browser = *v;
  if (auto v = table["desktop_file_manager"].value<std::string>()) config.desktop_file_manager = *v;
  if (auto v = table["desktop_text_editor"].value<std::string>()) config.desktop_text_editor = *v;
  if (auto v = table["shortcuts_help"].value<std::string>()) config.shortcuts_help = *v;
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
  table.insert_or_assign("shortcuts_help", config.shortcuts_help);
  table.insert_or_assign("start_menu_key", config.start_menu_key);
  table.insert_or_assign("desktop_terminal", config.desktop_terminal);
  table.insert_or_assign("desktop_browser", config.desktop_browser);
  table.insert_or_assign("desktop_file_manager", config.desktop_file_manager);
  table.insert_or_assign("desktop_text_editor", config.desktop_text_editor);

  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("failed to open " + path.string() + " for writing");
  }
  out << table << "\n";
}

}  // namespace fleetwm
