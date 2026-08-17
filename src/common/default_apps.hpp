#pragma once

#include <string>

namespace fleetwm {

// Config for the one default-app choice that has no XDG mimetype of its
// own: which terminal emulator Alt+Return spawns (compositor/input.cpp).
// Every other category in the Settings "Default Apps" tab (browser, file
// manager, image viewer, text editor, video player, PDF viewer, archive
// manager) is backed directly by GIO's g_app_info_set_as_default_for_type(),
// which reads/writes ~/.config/mimeapps.list itself -- the same file
// xdg-mime and every other XDG-aware app already reads, so those
// categories need no fleetwm-side storage at all.
struct DefaultAppsConfig {
  std::string terminal_command = "foot";
};

// Path helpers, mirroring wallpaper_config.hpp's own pair but for
// default_apps.toml instead.
std::string default_apps_user_config_path();
std::string default_apps_system_default_config_path();

// Loads default_apps.toml. Returns DefaultAppsConfig{} (terminal_command
// == "foot") if no config file exists anywhere yet, same fresh-install
// contract as load_wallpaper_config().
DefaultAppsConfig load_default_apps_config();

// Writes `config` to the user config path, creating parent directories
// as needed. Throws std::runtime_error on I/O failure.
void save_default_apps_config(const DefaultAppsConfig& config);

}  // namespace fleetwm
