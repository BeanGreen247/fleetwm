#include "battery_source.hpp"

namespace fleetwm {

namespace {
constexpr guint kPollIntervalMs = 15000;
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

}  // namespace fleetwm
