#include "power_config.hpp"

#include <toml++/toml.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "config_paths.hpp"

namespace fleetwm {

namespace fs = std::filesystem;

std::string power_user_config_path() {
  return (config_internal::config_home() / "fleetwm" / "power.toml").string();
}

namespace {

void read_profile(toml::table& root, const char* name, PowerProfile* out) {
  auto* t = root[name].as_table();
  if (!t) return;
  // as_integer() so a boolean or a string is ignored rather than converted.
  if (auto* v = (*t)["display_off_minutes"].as_integer())
    out->display_off_minutes = static_cast<int>(std::clamp<int64_t>(v->get(), 0, kMaxPowerMinutes));
  if (auto* v = (*t)["sleep_minutes"].as_integer())
    out->sleep_minutes = static_cast<int>(std::clamp<int64_t>(v->get(), 0, kMaxPowerMinutes));
}

toml::table profile_table(const PowerProfile& p) {
  toml::table t;
  t.insert_or_assign("display_off_minutes", static_cast<int64_t>(p.display_off_minutes));
  t.insert_or_assign("sleep_minutes", static_cast<int64_t>(p.sleep_minutes));
  return t;
}

}  // namespace

PowerConfig load_power_config() {
  PowerConfig config;
  const fs::path path = power_user_config_path();
  std::error_code ec;
  if (!fs::exists(path, ec)) return config;
  try {
    toml::table root = toml::parse_file(path.string());
    read_profile(root, "ac", &config.ac);
    read_profile(root, "battery", &config.battery);
  } catch (const toml::parse_error&) {
    return PowerConfig{};  // a broken file must never leave the machine without a working setting
  }
  return config;
}

void save_power_config(const PowerConfig& config) {
  const fs::path path = power_user_config_path();
  fs::create_directories(path.parent_path());
  toml::table root;
  root.insert_or_assign("ac", profile_table(config.ac));
  root.insert_or_assign("battery", profile_table(config.battery));
  std::ofstream out(path);
  if (!out) throw std::runtime_error("failed to open " + path.string() + " for writing");
  out << root << "\n";
}

const std::vector<int>& power_timeout_choices() {
  static const std::vector<int> kChoices = {0, 1, 2, 3, 5, 10, 15, 20, 30, 45, 60, 90, 120};
  return kChoices;
}

std::string power_timeout_label(int minutes) {
  if (minutes <= 0) return "Never";
  if (minutes == 1) return "1 minute";
  if (minutes < 60 || minutes % 60 != 0) return std::to_string(minutes) + " minutes";
  const int hours = minutes / 60;
  return hours == 1 ? "1 hour" : std::to_string(hours) + " hours";
}

int nearest_power_timeout_index(int minutes) {
  const std::vector<int>& choices = power_timeout_choices();
  if (minutes <= 0) return 0;  // "never" only matches itself
  int best = 1;
  for (size_t i = 1; i < choices.size(); ++i)
    if (std::abs(choices[i] - minutes) < std::abs(choices[static_cast<size_t>(best)] - minutes))
      best = static_cast<int>(i);
  return best;
}

IdleActions idle_actions(const PowerProfile& p, long idle_seconds) {
  IdleActions out;
  auto consider = [&](int minutes, bool* reached) {
    if (minutes <= 0) return;
    const long deadline = static_cast<long>(minutes) * 60;
    if (idle_seconds >= deadline) {
      *reached = true;
      return;
    }
    const long left = deadline - idle_seconds;
    if (out.next_check_seconds < 0 || left < out.next_check_seconds) out.next_check_seconds = left;
  };
  consider(p.display_off_minutes, &out.blank_display);
  consider(p.sleep_minutes, &out.suspend);
  return out;
}

}  // namespace fleetwm
