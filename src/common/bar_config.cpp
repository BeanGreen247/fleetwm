#include "bar_config.hpp"

#include <toml++/toml.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "paths_config.h"

namespace fleetwm {

namespace fs = std::filesystem;

namespace {

fs::path config_home() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
    return fs::path(xdg);
  }
  const char* home = std::getenv("HOME");
  if (!home) {
    throw std::runtime_error("HOME is not set; cannot resolve config path");
  }
  return fs::path(home) / ".config";
}

}  // namespace

std::string bar_user_config_path() {
  return (config_home() / "fleetwm" / "bar.toml").string();
}

std::string bar_system_default_config_path() {
  return std::string(FLEETWM_SYSCONF_DIR) + "/bar.toml";
}

BarConfig load_bar_config() {
  BarConfig config;

  fs::path path = bar_user_config_path();
  if (!fs::exists(path)) {
    path = bar_system_default_config_path();
    if (!fs::exists(path)) {
      return config;
    }
  }

  toml::table table = toml::parse_file(path.string());
  toml::table* clock = table["clock"].as_table();
  if (clock) {
    if (auto v = (*clock)["show_seconds"].value<bool>()) config.clock.show_seconds = *v;
    if (auto v = (*clock)["show_date"].value<bool>()) config.clock.show_date = *v;
    if (auto v = (*clock)["show_year"].value<bool>()) config.clock.show_year = *v;
    if (auto v = (*clock)["show_month"].value<bool>()) config.clock.show_month = *v;
    if (auto v = (*clock)["show_day"].value<bool>()) config.clock.show_day = *v;
  }

  toml::table* ws = table["workspace_colors"].as_table();
  if (ws) {
    if (auto v = (*ws)["inactive_bg"].value<std::string>()) config.workspace_colors.inactive_bg = *v;
    if (auto v = (*ws)["inactive_fg"].value<std::string>()) config.workspace_colors.inactive_fg = *v;
    if (auto v = (*ws)["active_bg"].value<std::string>()) config.workspace_colors.active_bg = *v;
    if (auto v = (*ws)["active_fg"].value<std::string>()) config.workspace_colors.active_fg = *v;
  }

  return config;
}

void save_bar_config(const BarConfig& config) {
  fs::path path = bar_user_config_path();
  fs::create_directories(path.parent_path());

  toml::table clock;
  clock.insert_or_assign("show_seconds", config.clock.show_seconds);
  clock.insert_or_assign("show_date", config.clock.show_date);
  clock.insert_or_assign("show_year", config.clock.show_year);
  clock.insert_or_assign("show_month", config.clock.show_month);
  clock.insert_or_assign("show_day", config.clock.show_day);

  toml::table workspace_colors;
  workspace_colors.insert_or_assign("inactive_bg", config.workspace_colors.inactive_bg);
  workspace_colors.insert_or_assign("inactive_fg", config.workspace_colors.inactive_fg);
  workspace_colors.insert_or_assign("active_bg", config.workspace_colors.active_bg);
  workspace_colors.insert_or_assign("active_fg", config.workspace_colors.active_fg);

  toml::table table;
  table.insert_or_assign("clock", clock);
  table.insert_or_assign("workspace_colors", workspace_colors);

  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("failed to open " + path.string() + " for writing");
  }
  out << table << "\n";
}

}  // namespace fleetwm
