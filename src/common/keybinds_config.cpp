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

int workspace_index_for_key(const std::string& key) {
  if (key.size() != 1 || key[0] < '0' || key[0] > '9') return -1;
  return key[0] == '0' ? 9 : key[0] - '1';
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
  if (auto v = table["desktop_debug_overlay"].value<std::string>()) config.desktop_debug_overlay = *v;
  if (auto v = table["desktop_snap_left"].value<std::string>()) config.desktop_snap_left = *v;
  if (auto v = table["desktop_snap_right"].value<std::string>()) config.desktop_snap_right = *v;
  if (auto v = table["desktop_snap_up"].value<std::string>()) config.desktop_snap_up = *v;
  if (auto v = table["desktop_snap_down"].value<std::string>()) config.desktop_snap_down = *v;
  if (auto v = table["desktop_close_window"].value<std::string>()) config.desktop_close_window = *v;
  if (auto v = table["desktop_toggle_maximize"].value<std::string>()) config.desktop_toggle_maximize = *v;
  if (auto v = table["desktop_show_desktop"].value<std::string>()) config.desktop_show_desktop = *v;
  if (auto v = table["desktop_minimize_all"].value<std::string>()) config.desktop_minimize_all = *v;
  if (auto v = table["desktop_restore_all"].value<std::string>()) config.desktop_restore_all = *v;
  if (auto v = table["cycle_windows"].value<std::string>()) config.cycle_windows = *v;
  if (auto v = table["cycle_windows_reverse"].value<std::string>()) config.cycle_windows_reverse = *v;
  if (auto v = table["send_to_prev_screen"].value<std::string>()) config.send_to_prev_screen = *v;
  if (auto v = table["send_to_next_screen"].value<std::string>()) config.send_to_next_screen = *v;
  if (auto v = table["workspace_switch"].value<std::string>()) config.workspace_switch = *v;
  if (auto v = table["workspace_send"].value<std::string>()) config.workspace_send = *v;
  if (auto v = table["workspace_prev"].value<std::string>()) config.workspace_prev = *v;
  if (auto v = table["workspace_next"].value<std::string>()) config.workspace_next = *v;
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
  table.insert_or_assign("desktop_debug_overlay", config.desktop_debug_overlay);
  table.insert_or_assign("desktop_snap_left", config.desktop_snap_left);
  table.insert_or_assign("desktop_snap_right", config.desktop_snap_right);
  table.insert_or_assign("desktop_snap_up", config.desktop_snap_up);
  table.insert_or_assign("desktop_snap_down", config.desktop_snap_down);
  table.insert_or_assign("desktop_close_window", config.desktop_close_window);
  table.insert_or_assign("desktop_toggle_maximize", config.desktop_toggle_maximize);
  table.insert_or_assign("desktop_show_desktop", config.desktop_show_desktop);
  table.insert_or_assign("desktop_minimize_all", config.desktop_minimize_all);
  table.insert_or_assign("desktop_restore_all", config.desktop_restore_all);
  table.insert_or_assign("cycle_windows", config.cycle_windows);
  table.insert_or_assign("cycle_windows_reverse", config.cycle_windows_reverse);
  table.insert_or_assign("send_to_prev_screen", config.send_to_prev_screen);
  table.insert_or_assign("send_to_next_screen", config.send_to_next_screen);
  table.insert_or_assign("workspace_switch", config.workspace_switch);
  table.insert_or_assign("workspace_send", config.workspace_send);
  table.insert_or_assign("workspace_prev", config.workspace_prev);
  table.insert_or_assign("workspace_next", config.workspace_next);

  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("failed to open " + path.string() + " for writing");
  }
  out << table << "\n";
}

}  // namespace fleetwm
