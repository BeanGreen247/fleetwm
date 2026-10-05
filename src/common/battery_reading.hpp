#pragma once

// GTK/GLib-free battery sysfs reading, shared by BatterySource (GLib timer,
// used by fleetwm-settings) and the fleetkit bar (its own timer).

#include <string>

namespace fleetwm {

struct BatteryReading {
  bool available = false;
  int percent = 0;
  bool charging = false;
  double hours_remaining = -1.0;  // -1 = unknown, 0 = full
};

// First /sys/class/power_supply/BATn directory, or "" if none.
std::string find_battery_dir();

// True when running on mains power: some Mains-type supply reports online=1,
// or no Mains supply is listed at all (desktops, VMs: assumed plugged in).
bool ac_online();

namespace battery_internal {
BatteryReading read_battery_reading(const std::string& battery_dir);
}  // namespace battery_internal

}  // namespace fleetwm
