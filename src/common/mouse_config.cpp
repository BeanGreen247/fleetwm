#include "mouse_config.hpp"

#include <toml++/toml.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "config_paths.hpp"

namespace fleetwm {

namespace fs = std::filesystem;

std::string mouse_user_config_path() { return (config_internal::config_home() / "fleetwm" / "mouse.toml").string(); }

double libinput_speed_for_notch(int notch) {
  const int n = std::clamp(notch, kMouseSpeedMin, kMouseSpeedMax);
  return (n - kMouseSpeedDefault) / static_cast<double>(kMouseSpeedMax - kMouseSpeedDefault);
}

MouseConfig load_mouse_config() {
  MouseConfig config;
  const fs::path path = mouse_user_config_path();
  std::error_code ec;
  if (!fs::exists(path, ec)) return config;
  try {
    toml::table root = toml::parse_file(path.string());
    if (auto v = root["speed"].value<int64_t>()) config.speed = std::clamp(static_cast<int>(*v), kMouseSpeedMin, kMouseSpeedMax);
    if (auto v = root["enhanced_precision"].value<bool>()) config.enhanced_precision = *v;
  } catch (const toml::parse_error&) {
    return MouseConfig{};
  }
  return config;
}

void save_mouse_config(const MouseConfig& config) {
  const fs::path path = mouse_user_config_path();
  fs::create_directories(path.parent_path());
  toml::table root;
  root.insert_or_assign("speed", static_cast<int64_t>(std::clamp(config.speed, kMouseSpeedMin, kMouseSpeedMax)));
  root.insert_or_assign("enhanced_precision", config.enhanced_precision);
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

}  // namespace fleetwm
