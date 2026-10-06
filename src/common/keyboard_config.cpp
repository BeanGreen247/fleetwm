#include "keyboard_config.hpp"

#include <toml++/toml.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "config_paths.hpp"

namespace fleetwm {

namespace fs = std::filesystem;

std::string keyboard_user_config_path() {
  return (config_internal::config_home() / "fleetwm" / "keyboard.toml").string();
}

namespace {
bool name_ok(const std::string& s) {
  return !s.empty() && s.size() <= 40 &&
         std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c == '.'; });
}
bool options_ok(const std::string& s) {  // comma-separated xkb options like "caps:escape,grp:alt_shift_toggle"
  return s.size() <= 200 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
           return std::isalnum(c) || c == '_' || c == '-' || c == ':' || c == ',' || c == '(' || c == ')' || c == '.';
         });
}
bool locale_ok(const std::string& s) {
  return !s.empty() && s.size() <= 40 &&
         std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == '@'; });
}
}  // namespace

bool parse_layout_spec(const std::string& spec, KeyboardLayout* out) {
  const size_t colon = spec.find(':');
  KeyboardLayout l;
  l.layout = spec.substr(0, colon);
  if (colon != std::string::npos) l.variant = spec.substr(colon + 1);
  if (!name_ok(l.layout) || (!l.variant.empty() && !name_ok(l.variant))) return false;
  *out = l;
  return true;
}

std::string layout_spec(const KeyboardLayout& l) { return l.variant.empty() ? l.layout : l.layout + ":" + l.variant; }

const char* switch_keys_name(LayoutSwitchKeys k) {
  switch (k) {
    case LayoutSwitchKeys::SuperSpace: return "super_space";
    case LayoutSwitchKeys::AltShift: return "alt_shift";
    default: return "both";
  }
}
LayoutSwitchKeys switch_keys_from_name(const std::string& s) {
  if (s == "super_space") return LayoutSwitchKeys::SuperSpace;
  if (s == "alt_shift") return LayoutSwitchKeys::AltShift;
  return LayoutSwitchKeys::Both;
}

KeyboardConfig load_keyboard_config() {
  KeyboardConfig config;
  const fs::path path = keyboard_user_config_path();
  std::error_code ec;
  if (!fs::exists(path, ec)) return config;
  try {
    toml::table root = toml::parse_file(path.string());
    if (auto* arr = root["layouts"].as_array()) {
      std::vector<KeyboardLayout> layouts;
      for (auto& el : *arr) {
        KeyboardLayout l;
        if (auto s = el.value<std::string>(); s && parse_layout_spec(*s, &l) &&
            std::find(layouts.begin(), layouts.end(), l) == layouts.end() && layouts.size() < 16)
          layouts.push_back(l);
      }
      if (!layouts.empty()) config.layouts = layouts;
    }
    if (auto v = root["model"].value<std::string>(); v && (v->empty() || name_ok(*v))) config.model = *v;
    if (auto v = root["options"].value<std::string>(); v && options_ok(*v)) config.options = *v;
    if (auto v = root["switch_keys"].value<std::string>()) config.switch_keys = switch_keys_from_name(*v);
    if (auto* v = root["repeat_rate"].as_integer()) config.repeat_rate = static_cast<int>(std::clamp<int64_t>(v->get(), 1, 100));
    if (auto* v = root["repeat_delay"].as_integer()) config.repeat_delay = static_cast<int>(std::clamp<int64_t>(v->get(), 150, 2000));
    if (auto* arr = root["locales"].as_array())
      for (auto& el : *arr)
        if (auto s = el.value<std::string>(); s && locale_ok(*s) && std::find(config.locales.begin(), config.locales.end(), *s) == config.locales.end())
          config.locales.push_back(*s);
  } catch (const toml::parse_error&) {
    return KeyboardConfig{};  // a broken file must never leave the keyboard unusable
  }
  return config;
}

void save_keyboard_config(const KeyboardConfig& config) {
  const fs::path path = keyboard_user_config_path();
  fs::create_directories(path.parent_path());
  toml::table root;
  toml::array layouts;
  for (const KeyboardLayout& l : config.layouts) layouts.push_back(layout_spec(l));
  root.insert_or_assign("layouts", layouts);
  root.insert_or_assign("model", config.model);
  root.insert_or_assign("options", config.options);
  root.insert_or_assign("switch_keys", switch_keys_name(config.switch_keys));
  root.insert_or_assign("repeat_rate", static_cast<int64_t>(config.repeat_rate));
  root.insert_or_assign("repeat_delay", static_cast<int64_t>(config.repeat_delay));
  toml::array locales;
  for (const std::string& l : config.locales) locales.push_back(l);
  root.insert_or_assign("locales", locales);
  const fs::path tmp = path.string() + ".tmp";  // write beside it, then rename: watchers never see half a file
  {
    std::ofstream out(tmp);
    if (!out) throw std::runtime_error("cannot write " + tmp.string());
    out << root;
    if (!out) throw std::runtime_error("cannot write " + tmp.string());
  }
  std::error_code ec;
  fs::rename(tmp, path, ec);
  if (ec) throw std::runtime_error("cannot save " + path.string() + ": " + ec.message());
}

XkbNames xkb_names_for(const KeyboardConfig& config) {
  XkbNames n;
  for (size_t i = 0; i < config.layouts.size(); ++i) {
    if (i) {
      n.layout += ",";
      n.variant += ",";
    }
    n.layout += config.layouts[i].layout;
    n.variant += config.layouts[i].variant;
  }
  n.model = config.model;
  n.options = config.options;
  return n;
}

std::string layout_pill_text(const KeyboardLayout& l) {
  std::string t = l.layout.substr(0, 3);
  std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::toupper(c); });
  return t;
}

}  // namespace fleetwm
