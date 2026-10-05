#include "theme.hpp"

#include <toml++/toml.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "config_paths.hpp"
#include "paths_config.h"

namespace fleetwm {

namespace fs = std::filesystem;
using config_internal::config_home;

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

std::string window_layout_to_string(WindowLayout layout) {
  return layout == WindowLayout::Desktop ? "desktop" : "tiling";
}

WindowLayout window_layout_from_string(const std::string& s) {
  return s == "desktop" ? WindowLayout::Desktop : WindowLayout::Tiling;
}

std::string button_side_to_string(ButtonSide side) {
  return side == ButtonSide::Left ? "left" : "right";
}

ButtonSide button_side_from_string(const std::string& s) {
  return s == "left" ? ButtonSide::Left : ButtonSide::Right;
}

std::string title_align_to_string(TitleAlign align) {
  switch (align) {
    case TitleAlign::Left: return "left";
    case TitleAlign::Right: return "right";
    case TitleAlign::Center: break;
  }
  return "center";
}

TitleAlign title_align_from_string(const std::string& s) {
  if (s == "left") return TitleAlign::Left;
  if (s == "right") return TitleAlign::Right;
  return TitleAlign::Center;
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
  if (auto* t = table["titlebar"].as_table()) {
    TitlebarConfig& tb = config.titlebar;
    if (auto v = (*t)["height"].value<int64_t>()) tb.height = std::clamp(static_cast<int>(*v), 20, 64);
    if (auto v = (*t)["button_width"].value<int64_t>()) tb.button_width = std::clamp(static_cast<int>(*v), 20, 80);
    if (auto v = (*t)["button_height"].value<int64_t>()) tb.button_height = static_cast<int>(*v);
    tb.button_height = std::clamp(tb.button_height, 14, tb.height);
    if (auto v = (*t)["buttons_side"].value<std::string>()) tb.buttons_side = button_side_from_string(*v);
    if (auto v = (*t)["title_align"].value<std::string>()) tb.title_align = title_align_from_string(*v);
    if (auto v = (*t)["show_pin"].value<bool>()) tb.show_pin = *v;
    if (auto v = (*t)["show_minimize"].value<bool>()) tb.show_minimize = *v;
    if (auto v = (*t)["show_maximize"].value<bool>()) tb.show_maximize = *v;
  }
  if (auto v = table["window_layout"].value<std::string>()) {
    config.window_layout = window_layout_from_string(*v);
  }
  if (auto v = table["gap_px"].value<int64_t>()) {
    config.gap_px = std::clamp(static_cast<int>(*v), 0, 64);
    // Older files had a single gap that also framed the screen edges.
    config.outer_gap_px = config.gap_px;
  }
  if (auto v = table["outer_gap_px"].value<int64_t>()) {
    config.outer_gap_px = std::clamp(static_cast<int>(*v), 0, 64);
  }
  if (auto v = table["bar_gap_px"].value<int64_t>()) {
    config.bar_gap_px = std::clamp(static_cast<int>(*v), 0, 64);
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
  if (auto v = table["render_mode"].value<std::string>()) {
    config.render_mode = (*v == "custom") ? RenderMode::Custom : RenderMode::Synced;
  }
  if (auto v = table["custom_fps_lock"].value<int64_t>()) {
    config.custom_fps_lock = std::clamp(static_cast<int>(*v), 24, 5000);
  }
  if (auto v = table["show_debug_overlay_on_startup"].value<bool>()) {
    config.show_debug_overlay_on_startup = *v;
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
  table.insert_or_assign("outer_gap_px", static_cast<int64_t>(config.outer_gap_px));
  table.insert_or_assign("bar_gap_px", static_cast<int64_t>(config.bar_gap_px));
  table.insert_or_assign("window_layout", window_layout_to_string(config.window_layout));
  table.insert_or_assign("pinned_border_color", config.pinned_border_color);
  table.insert_or_assign("pinned_focused_border_color", config.pinned_focused_border_color);
  table.insert_or_assign("pinned_border_thickness_px",
                          static_cast<int64_t>(config.pinned_border_thickness_px));
  table.insert_or_assign("render_mode",
                          config.render_mode == RenderMode::Custom ? "custom" : "synced");
  table.insert_or_assign("custom_fps_lock", static_cast<int64_t>(config.custom_fps_lock));
  table.insert_or_assign("show_debug_overlay_on_startup", config.show_debug_overlay_on_startup);

  const TitlebarConfig& tb = config.titlebar;
  toml::table titlebar;
  titlebar.insert_or_assign("height", static_cast<int64_t>(tb.height));
  titlebar.insert_or_assign("button_width", static_cast<int64_t>(tb.button_width));
  titlebar.insert_or_assign("button_height", static_cast<int64_t>(tb.button_height));
  titlebar.insert_or_assign("buttons_side", button_side_to_string(tb.buttons_side));
  titlebar.insert_or_assign("title_align", title_align_to_string(tb.title_align));
  titlebar.insert_or_assign("show_pin", tb.show_pin);
  titlebar.insert_or_assign("show_minimize", tb.show_minimize);
  titlebar.insert_or_assign("show_maximize", tb.show_maximize);
  table.insert_or_assign("titlebar", std::move(titlebar));

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
