#pragma once

#include <string>

// TuxThrottle (github.com/BeanGreen247/tuxthrottle): the gaming-laptop tool with a root daemon for power profiles, fans, TDP and a "Game Mode".
// Fleetwm talks to its CLI, `tuxthrottlectl`, which reads without privileges (`status --json`) and changes things through the narrow sudo/pkexec rules
// TuxThrottle installs (`gamemode on|off`). Nothing here needs TuxThrottle: without `tuxthrottlectl` every function is a no-op.
//   * While any game runs (the same windows that are registered with Feral GameMode) the compositor switches TuxThrottle's Game Mode on and off again after
//     the last one closes: performance profile, fan boost, GPU clocks, whatever the user's helper scripts do (game.toml: tuxthrottle).
//   * Settings -> Gaming shows TuxThrottle's readout and a Game Mode switch.

namespace fleetwm {

struct TuxThrottleStatus {
  bool ok = false;                // the readout was obtained and understood
  std::string model;              // "Dell G15 5515"
  std::string platform_profile;   // "quiet" | "balanced" | "performance" ...
  double cpu_temp_c = -1, cpu_power_w = -1;
  bool dgpu_present = false, dgpu_awake = false;
  double dgpu_temp_c = -1, dgpu_power_w = -1;
  bool game_mode = false;         // TuxThrottle's Game Mode (helper scripts applied)
  bool gamemoded_installed = false;
  int gamemoded_clients = 0;
};

// Parses the output of `tuxthrottlectl status --json`. ok stays false for text that is not that JSON.
TuxThrottleStatus parse_tuxthrottle_status(const std::string& json);

bool tuxthrottle_installed();                 // `tuxthrottlectl` is on PATH
TuxThrottleStatus read_tuxthrottle_status();  // runs `tuxthrottlectl status --json` (about 0.3 s); ok=false when not installed or it failed
// Starts `tuxthrottlectl gamemode on|off|toggle` and returns at once (the call can take a second; its result is not waited for).
void tuxthrottle_game_mode(const std::string& action);
// True when /etc/gamemode.ini (or the user's own) runs TuxThrottle's helper scripts from gamemoded's start hook, i.e. GameMode alone already triggers them.
bool tuxthrottle_bridged_through_gamemode();

}  // namespace fleetwm
