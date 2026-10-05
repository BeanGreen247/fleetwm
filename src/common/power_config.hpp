#pragma once

#include <string>
#include <vector>

// Power settings (Settings -> Power), saved per user in power.toml: when to turn the
// display off and when to put the computer to sleep, separately for mains power and
// for battery. 0 means never.

namespace fleetwm {

struct PowerProfile {
  int display_off_minutes = 10;
  int sleep_minutes = 0;
  bool operator==(const PowerProfile&) const = default;
};

struct PowerConfig {
  PowerProfile ac{15, 0};       // plugged in: screen off after 15 minutes, never sleep
  PowerProfile battery{5, 15};  // on battery: screen off after 5, sleep after 15
  bool operator==(const PowerConfig&) const = default;
};

constexpr int kMaxPowerMinutes = 720;

std::string power_user_config_path();

// Loads power.toml; missing file or bad values give the defaults above. Minutes are
// clamped to 0..720.
PowerConfig load_power_config();

// Writes power.toml, creating the directory. Throws std::runtime_error on I/O failure.
void save_power_config(const PowerConfig& config);

// The choices offered in Settings, in order: 0 (never), then 1 minute up to 2 hours.
const std::vector<int>& power_timeout_choices();

// "Never", "1 minute", "15 minutes", "1 hour", "2 hours", "90 minutes".
std::string power_timeout_label(int minutes);

// Index into power_timeout_choices() of the choice closest to `minutes` (so an
// hand-edited 7 shows as 5 or 10 instead of nothing).
int nearest_power_timeout_index(int minutes);

// What the idle monitor should do after `idle_seconds` without input.
struct IdleActions {
  bool blank_display = false;  // the display-off time has passed
  bool suspend = false;        // the sleep time has passed
  // Seconds until the next time something changes (a deadline not yet reached), or -1
  // when nothing more will ever happen with this profile.
  long next_check_seconds = -1;
};
IdleActions idle_actions(const PowerProfile& profile, long idle_seconds);

}  // namespace fleetwm
