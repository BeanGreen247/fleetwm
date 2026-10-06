#pragma once

#include <string>
#include <vector>

#include "power_config.hpp"

// The text the lock applet shows when you hover its padlock: what is switched off or on
// right now, and which programs are keeping the computer awake.

namespace fleetwm {

struct LockHolder {
  std::string name;    // program name ("mpv", "firefox")
  std::string reason;  // why, when it said ("Playing video"); may be empty
};

struct LockTooltip {
  std::string title;
  std::string body;  // several lines, no trailing newline
};

// `keep_awake`: the user switched it on from the padlock. `holders`: programs other than
// the applet that hold the screen on or block sleep. `on_battery` picks the power
// profile shown.
LockTooltip describe_lock_state(bool keep_awake, const std::vector<LockHolder>& holders,
                                const PowerConfig& power, bool on_battery);

// "mpv - Playing video", or just "mpv".
std::string describe_holder(const LockHolder& holder);

// Removes entries whose name (ignoring case) appeared before, keeping the first reason.
std::vector<LockHolder> unique_holders(std::vector<LockHolder> holders);

}  // namespace fleetwm
