#include "output_config.hpp"

#include <toml++/toml.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "config_paths.hpp"

namespace fleetwm {

namespace fs = std::filesystem;

std::string output_config_path() {
  return (config_internal::config_home() / "fleetwm" / "outputs.toml").string();
}

OutputSettings load_output_settings() {
  OutputSettings out;
  const fs::path path = output_config_path();
  if (!fs::exists(path)) {
    return out;
  }
  toml::table root;
  try {
    root = toml::parse_file(path.string());
  } catch (const toml::parse_error&) {
    return out;  // a corrupt file must never stop the compositor from starting
  }
  const toml::table* outputs = root["outputs"].as_table();
  if (!outputs) {
    return out;
  }
  for (const auto& [name, node] : *outputs) {
    const toml::table* t = node.as_table();
    if (!t) {
      continue;
    }
    OutputSetting s;
    s.width = (*t)["width"].value_or(0);
    s.height = (*t)["height"].value_or(0);
    s.refresh_mhz = (*t)["refresh_mhz"].value_or(0);
    if (t->contains("x") && t->contains("y")) {
      s.has_pos = true;
      s.x = (*t)["x"].value_or(0);
      s.y = (*t)["y"].value_or(0);
    }
    if (s.width < 0 || s.height < 0 || s.refresh_mhz < 0) {
      continue;
    }
    out[std::string(name.str())] = s;
  }
  return out;
}

DisplaySettings load_display_settings() {
  DisplaySettings out;
  const fs::path path = output_config_path();
  if (!fs::exists(path)) return out;
  try {
    const toml::table root = toml::parse_file(path.string());
    out.primary_output = root["primary_output"].value_or(std::string());
    out.taskbar_all_displays = root["taskbar_all_displays"].value_or(true);
  } catch (const toml::parse_error&) {
  }
  return out;
}

void save_output_settings(const OutputSettings& settings) {
  const fs::path path = output_config_path();
  fs::create_directories(path.parent_path());

  toml::table outputs;
  for (const auto& [name, s] : settings) {
    toml::table t;
    if (s.width > 0 && s.height > 0) {
      t.insert_or_assign("width", s.width);
      t.insert_or_assign("height", s.height);
      if (s.refresh_mhz > 0) {
        t.insert_or_assign("refresh_mhz", s.refresh_mhz);
      }
    }
    if (s.has_pos) {
      t.insert_or_assign("x", s.x);
      t.insert_or_assign("y", s.y);
    }
    outputs.insert_or_assign(name, std::move(t));
  }
  toml::table root;
  const DisplaySettings display = load_display_settings();
  if (!display.primary_output.empty()) root.insert_or_assign("primary_output", display.primary_output);
  root.insert_or_assign("taskbar_all_displays", display.taskbar_all_displays);
  root.insert_or_assign("outputs", std::move(outputs));

  const fs::path tmp = path.string() + ".tmp";
  {
    std::ofstream out(tmp);
    if (!out) {
      throw std::runtime_error("failed to open " + tmp.string() + " for writing");
    }
    out << root << "\n";
  }
  fs::rename(tmp, path);  // atomic: inotify watchers never see a half-written file
}

void save_display_settings(const DisplaySettings& settings) {
  OutputSettings outputs = load_output_settings();
  const fs::path path = output_config_path();
  fs::create_directories(path.parent_path());
  toml::table output_table;
  for (const auto& [name, s] : outputs) {
    toml::table t;
    if (s.width > 0 && s.height > 0) {
      t.insert_or_assign("width", s.width);
      t.insert_or_assign("height", s.height);
      if (s.refresh_mhz > 0) t.insert_or_assign("refresh_mhz", s.refresh_mhz);
    }
    if (s.has_pos) {
      t.insert_or_assign("x", s.x);
      t.insert_or_assign("y", s.y);
    }
    output_table.insert_or_assign(name, std::move(t));
  }
  toml::table root;
  if (!settings.primary_output.empty()) root.insert_or_assign("primary_output", settings.primary_output);
  root.insert_or_assign("taskbar_all_displays", settings.taskbar_all_displays);
  root.insert_or_assign("outputs", std::move(output_table));
  const fs::path tmp = path.string() + ".tmp";
  {
    std::ofstream out(tmp);
    if (!out) throw std::runtime_error("failed to open " + tmp.string() + " for writing");
    out << root << "\n";
  }
  fs::rename(tmp, path);
}

}  // namespace fleetwm
