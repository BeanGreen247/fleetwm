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

// The real power-supply directory in sysfs.
inline constexpr const char* kPowerSupplyDir = "/sys/class/power_supply";

// First BATn directory under `supply_dir`, or "" if none. The directory argument
// exists so tests can point it at a fake tree instead of the machine's real one.
std::string find_battery_dir(const std::string& supply_dir = kPowerSupplyDir);

// True when running on mains power: some Mains-type supply reports online=1,
// or no Mains supply is listed at all (desktops, VMs: assumed plugged in).
bool ac_online(const std::string& supply_dir = kPowerSupplyDir);

// The wording for a battery, shared by the bar's tooltip and Settings -> Power:
// `headline` is "87% - charging" and `detail` the time line ("1h 20m until full").
struct BatteryText {
  std::string headline;
  std::string detail;
};
BatteryText describe_battery(const BatteryReading& reading, bool on_ac);

// What the bar's battery icon fills: `fraction` of the body (0..1) and its colour. Red under 10%,
// green when full, the icon colour otherwise. While charging (and not full) the fill sweeps from the
// real level to the right edge as `phase` goes 0..steps; phase 0 shows the real level.
enum class BatteryFillColor { Normal, Green, Red };
struct BatteryFill {
  double fraction = 0.0;
  BatteryFillColor color = BatteryFillColor::Normal;
};
BatteryFill battery_fill(const BatteryReading& reading, int phase, int steps);

namespace battery_internal {
BatteryReading read_battery_reading(const std::string& battery_dir);
}  // namespace battery_internal

}  // namespace fleetwm
