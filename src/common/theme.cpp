#include "theme.hpp"

#include <toml++/toml.h>

#include <cctype>
#include <cstdio>
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

std::string user_config_path() {
  return (config_home() / "fleetwm" / "theme.toml").string();
}

std::string system_default_config_path() {
  return std::string(FLEETWM_SYSCONF_DIR) + "/theme.toml";
}

std::string themes_dir() {
  return std::string(FLEETWM_SYSCONF_DIR) + "/themes";
}

std::string theme_name_to_string(ThemeName theme) {
  switch (theme) {
    case ThemeName::Dark: return "dark";
    case ThemeName::Catppuccin: return "catppuccin";
    case ThemeName::Dracula: return "dracula";
    case ThemeName::OledBlack: return "oled_black";
    case ThemeName::Light: return "light";
  }
  return "dark";
}

ThemeName theme_name_from_string(const std::string& s) {
  if (s == "catppuccin") return ThemeName::Catppuccin;
  if (s == "dracula") return ThemeName::Dracula;
  if (s == "oled_black") return ThemeName::OledBlack;
  if (s == "light") return ThemeName::Light;
  return ThemeName::Dark;
}

std::string theme_css_filename(ThemeName theme) {
  return theme_name_to_string(theme) + ".css";
}

ThemeConfig load_theme_config() {
  ThemeConfig config;

  fs::path path = user_config_path();
  if (!fs::exists(path)) {
    path = system_default_config_path();
    if (!fs::exists(path)) {
      // No config anywhere yet (packaging step hasn't run, or this is a
      // dev build run out-of-tree) -- ship sane defaults rather than fail.
      return config;
    }
  }

  toml::table table = toml::parse_file(path.string());

  if (auto v = table["corner_style"].value<std::string>()) {
    config.corner_style = (*v == "sharp") ? CornerStyle::Sharp : CornerStyle::Rounded;
  }
  if (auto v = table["theme"].value<std::string>()) {
    config.theme = theme_name_from_string(*v);
  }
  if (auto v = table["accent"].value<std::string>()) {
    if (*v == "auto") {
      config.accent.auto_extract = true;
    } else {
      config.accent.auto_extract = false;
      config.accent.hex = *v;
    }
  }
  if (auto v = table["focus_border_thickness_px"].value<int64_t>()) {
    config.focus_border_thickness_px = static_cast<int>(*v);
  }
  if (auto v = table["focus_border_color"].value<std::string>()) {
    config.focus_border_color = *v;
  }
  if (auto v = table["gap_px"].value<int64_t>()) {
    config.gap_px = static_cast<int>(*v);
  }
  if (auto v = table["pinned_border_color"].value<std::string>()) {
    config.pinned_border_color = *v;
  }
  if (auto v = table["pinned_focused_border_color"].value<std::string>()) {
    config.pinned_focused_border_color = *v;
  }
  if (auto v = table["pinned_border_thickness_px"].value<int64_t>()) {
    config.pinned_border_thickness_px = static_cast<int>(*v);
  }

  return config;
}

void save_theme_config(const ThemeConfig& config) {
  fs::path path = user_config_path();
  fs::create_directories(path.parent_path());

  toml::table table;
  table.insert_or_assign(
      "corner_style", config.corner_style == CornerStyle::Sharp ? "sharp" : "rounded");
  table.insert_or_assign("theme", theme_name_to_string(config.theme));
  table.insert_or_assign("accent", config.accent.auto_extract ? "auto" : config.accent.hex);
  table.insert_or_assign("focus_border_thickness_px",
                          static_cast<int64_t>(config.focus_border_thickness_px));
  table.insert_or_assign("focus_border_color", config.focus_border_color);
  table.insert_or_assign("gap_px", static_cast<int64_t>(config.gap_px));
  table.insert_or_assign("pinned_border_color", config.pinned_border_color);
  table.insert_or_assign("pinned_focused_border_color", config.pinned_focused_border_color);
  table.insert_or_assign("pinned_border_thickness_px",
                          static_cast<int64_t>(config.pinned_border_thickness_px));

  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("failed to open " + path.string() + " for writing");
  }
  out << table << "\n";
}

bool parse_hex_color(const std::string& hex, float out_rgba[4]) {
  if (hex.size() != 7 || hex[0] != '#') {
    return false;
  }
  // Reject anything but plain hex digits up front -- sscanf's "%2x"
  // conversions each skip leading whitespace by themselves (standard
  // scanf behavior, not specific to %x), so e.g. "#89 4fa" previously
  // sailed through as r=0x89 g=0x4f b=0x0a instead of being rejected as
  // malformed, since the embedded space was silently consumed between
  // the first and second conversion. Found by a test exercising exactly
  // that string.
  for (size_t i = 1; i < hex.size(); ++i) {
    if (!std::isxdigit(static_cast<unsigned char>(hex[i]))) {
      return false;
    }
  }
  unsigned int r, g, b;
  // %2x consumes exactly two hex digits per component; sscanf returning
  // fewer than 3 conversions means a malformed string (non-hex chars,
  // early null, etc.) rather than a valid #rrggbb.
  if (std::sscanf(hex.c_str() + 1, "%2x%2x%2x", &r, &g, &b) != 3) {
    return false;
  }
  out_rgba[0] = static_cast<float>(r) / 255.0f;
  out_rgba[1] = static_cast<float>(g) / 255.0f;
  out_rgba[2] = static_cast<float>(b) / 255.0f;
  out_rgba[3] = 1.0f;
  return true;
}

}  // namespace fleetwm
