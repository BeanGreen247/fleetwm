#pragma once

#include <glib.h>

#include <functional>
#include <string>

namespace fleetwm {

// Reports the system's primary battery state by polling
// /sys/class/power_supply/BAT* -- no vendor daemon dependency (mirrors
// ADR 0005's "no heavy dependencies for a handful of numbers" stance),
// same sysfs-polling shape as the GPU%/Disk% sources there. Battery state
// changes slowly, so this polls on its own timer rather than the 1Hz
// clock/CPU tick (ADR 0005 precedent: Disk% also gets its own slower
// timer for the same reason).
//
// On a desktop with no battery present, `available` is always reported
// false and the bar/settings hide the battery UI entirely.
class BatterySource {
 public:
  struct Reading {
    bool available = false;
    int percent = 0;
    bool charging = false;
    // -1 when the kernel hasn't published enough data yet (e.g. right
    // after a charge-state transition) to estimate a rate.
    double hours_remaining = -1.0;
  };

  using Callback = std::function<void(const Reading&)>;

  // Probes for a battery once, then starts polling and delivers updates
  // via `on_update` (called once immediately, then again on every poll).
  // `on_update` is stored and called for the lifetime of this object --
  // must outlive it.
  void start(Callback on_update);

  // Static probe, usable without constructing/starting a full instance
  // (Settings needs to know whether to show the Power tab at all before
  // it cares about live readings).
  static bool battery_present();

  ~BatterySource();

 private:
  static gboolean on_poll_tick(gpointer user_data);
  void poll_once();

  Callback on_update_;
  std::string battery_dir_;  // e.g. "/sys/class/power_supply/BAT0", empty if none found
  guint timer_id_ = 0;
};

// Exposed purely so unit tests can exercise the sysfs-parsing/rate-math
// logic against a fake directory of files instead of a real battery --
// not part of the public API, do not call from application code.
namespace battery_internal {
BatterySource::Reading read_battery_reading(const std::string& battery_dir);
}  // namespace battery_internal

}  // namespace fleetwm
