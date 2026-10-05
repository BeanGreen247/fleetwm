#include "battery_reading.hpp"

#include <dirent.h>

#include <cstring>
#include <fstream>

namespace fleetwm {

namespace {

bool read_sysfs_value(const std::string& path, long long* out) {
  std::ifstream f(path);
  if (!(f >> *out)) {
    return false;
  }
  return true;
}

bool read_sysfs_string(const std::string& path, std::string* out) {
  std::ifstream f(path);
  return static_cast<bool>(std::getline(f, *out));
}

}  // namespace

// First BATn directory found under /sys/class/power_supply, or "" if
// none exists -- covers both "no battery" (desktop) and "battery present
// but not yet enumerated" the same way, since either just yields "".
std::string find_battery_dir(const std::string& supply_dir) {
  DIR* dir = opendir(supply_dir.c_str());
  if (!dir) {
    return "";
  }
  std::string found;
  while (dirent* entry = readdir(dir)) {
    if (std::strncmp(entry->d_name, "BAT", 3) == 0) {
      found = supply_dir + "/" + entry->d_name;
      break;
    }
  }
  closedir(dir);
  return found;
}

bool ac_online(const std::string& supply_dir) {
  DIR* dir = opendir(supply_dir.c_str());
  if (!dir) {
    return true;
  }
  bool any_mains = false, online = false;
  while (dirent* entry = readdir(dir)) {
    if (entry->d_name[0] == '.') {
      continue;
    }
    const std::string base = supply_dir + "/" + entry->d_name;
    std::string type;
    if (!read_sysfs_string(base + "/type", &type) || type != "Mains") {
      continue;
    }
    any_mains = true;
    long long value = 0;
    if (read_sysfs_value(base + "/online", &value) && value == 1) {
      online = true;
    }
  }
  closedir(dir);
  return any_mains ? online : true;
}

BatteryText describe_battery(const BatteryReading& r, bool on_ac) {
  if (!r.available) return {on_ac ? "On AC power" : "No battery", ""};
  const bool full = !r.charging && on_ac && r.hours_remaining == 0.0;
  BatteryText t;
  t.headline = std::to_string(r.percent) + "% - ";
  t.headline += r.charging ? "charging" : full ? "fully charged" : on_ac ? "plugged in, not charging" : "on battery";
  if (full) {
    t.detail = "Time left: not applicable (full)";
  } else if (r.hours_remaining > 0.0) {
    const int mins = static_cast<int>(r.hours_remaining * 60.0 + 0.5);
    t.detail = std::to_string(mins / 60) + "h " + std::to_string(mins % 60) + "m " +
               (r.charging ? "until full" : "remaining");
  } else if (!r.charging && on_ac) {
    t.detail = "Time left: not discharging";
  } else {
    t.detail = std::string(r.charging ? "Time until full" : "Time remaining") + ": calculating...";
  }
  return t;
}

namespace battery_internal {

BatteryReading read_battery_reading(const std::string& battery_dir) {
  BatteryReading reading;
  if (battery_dir.empty()) {
    return reading;
  }

  long long capacity = 0;
  if (!read_sysfs_value(battery_dir + "/capacity", &capacity)) {
    return BatteryReading{};  // battery vanished (e.g. hot-unplug) -- degrade gracefully
  }
  reading.available = true;
  reading.percent = static_cast<int>(capacity);

  std::string status;
  read_sysfs_string(battery_dir + "/status", &status);
  reading.charging = (status == "Charging");
  bool full = (status == "Full");

  // Energy-based (µWh/µW) is preferred; some kernels/drivers only expose
  // the charge-based (µAh/µA) equivalents instead -- same rate math
  // either way since the units cancel out in the ratio.
  long long now = 0, full_val = 0, rate = 0;
  bool have_now = read_sysfs_value(battery_dir + "/energy_now", &now) &&
                  read_sysfs_value(battery_dir + "/power_now", &rate);
  if (!have_now) {
    have_now = read_sysfs_value(battery_dir + "/charge_now", &now) &&
               read_sysfs_value(battery_dir + "/current_now", &rate);
  }
  bool have_full = read_sysfs_value(battery_dir + "/energy_full", &full_val) ||
                    read_sysfs_value(battery_dir + "/charge_full", &full_val);

  if (full || rate == 0 || !have_now) {
    reading.hours_remaining = full ? 0.0 : -1.0;
  } else if (reading.charging && have_full) {
    reading.hours_remaining =
        static_cast<double>(full_val - now) / static_cast<double>(rate);
  } else if (!reading.charging) {
    reading.hours_remaining = static_cast<double>(now) / static_cast<double>(rate);
  } else {
    reading.hours_remaining = -1.0;
  }

  return reading;
}

}  // namespace battery_internal

}  // namespace fleetwm
