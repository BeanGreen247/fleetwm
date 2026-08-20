#include "battery_source.hpp"

#include <dirent.h>

#include <cstring>
#include <fstream>
#include <sstream>

namespace fleetwm {

namespace {

constexpr guint kPollIntervalMs = 15000;
constexpr const char* kPowerSupplyDir = "/sys/class/power_supply";

// First BATn directory found under /sys/class/power_supply, or "" if
// none exists -- covers both "no battery" (desktop) and "battery present
// but not yet enumerated" the same way, since either just yields "".
std::string find_battery_dir() {
  DIR* dir = opendir(kPowerSupplyDir);
  if (!dir) {
    return "";
  }
  std::string found;
  while (dirent* entry = readdir(dir)) {
    if (std::strncmp(entry->d_name, "BAT", 3) == 0) {
      found = std::string(kPowerSupplyDir) + "/" + entry->d_name;
      break;
    }
  }
  closedir(dir);
  return found;
}

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

bool BatterySource::battery_present() {
  return !find_battery_dir().empty();
}

void BatterySource::start(Callback on_update) {
  on_update_ = std::move(on_update);
  battery_dir_ = find_battery_dir();
  poll_once();
  timer_id_ = g_timeout_add(kPollIntervalMs, on_poll_tick, this);
}

BatterySource::~BatterySource() {
  if (timer_id_ != 0) {
    g_source_remove(timer_id_);
  }
}

gboolean BatterySource::on_poll_tick(gpointer user_data) {
  static_cast<BatterySource*>(user_data)->poll_once();
  return G_SOURCE_CONTINUE;
}

void BatterySource::poll_once() {
  on_update_(battery_internal::read_battery_reading(battery_dir_));
}

namespace battery_internal {

BatterySource::Reading read_battery_reading(const std::string& battery_dir) {
  BatterySource::Reading reading;
  if (battery_dir.empty()) {
    return reading;
  }

  long long capacity = 0;
  if (!read_sysfs_value(battery_dir + "/capacity", &capacity)) {
    return BatterySource::Reading{};  // battery vanished (e.g. hot-unplug) -- degrade gracefully
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
