#include "lock_tooltip.hpp"

#include <algorithm>
#include <cctype>

namespace fleetwm {

namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

std::string timer_line(const char* what, int minutes, bool paused) {
  if (paused) return std::string(what) + ": paused";
  return std::string(what) + ": " + (minutes == 0 ? "never" : "after " + power_timeout_label(minutes));
}

}  // namespace

std::string describe_holder(const LockHolder& holder) {
  return holder.reason.empty() ? holder.name : holder.name + " - " + holder.reason;
}

std::vector<LockHolder> unique_holders(std::vector<LockHolder> holders) {
  std::vector<LockHolder> out;
  for (LockHolder& h : holders) {
    const std::string key = lower(h.name);
    const bool seen = std::any_of(out.begin(), out.end(), [&](const LockHolder& o) { return lower(o.name) == key; });
    if (!seen) out.push_back(std::move(h));
  }
  return out;
}

LockTooltip describe_lock_state(bool keep_awake, const std::vector<LockHolder>& holders,
                                const PowerConfig& power, bool on_battery) {
  const PowerProfile& profile = on_battery ? power.battery : power.ac;
  const bool blocked = keep_awake || !holders.empty();
  LockTooltip t;
  t.title = keep_awake ? "Keep awake is on" : "Keep awake is off";

  t.body = timer_line("Screen off", profile.display_off_minutes, blocked);
  t.body += "\n" + timer_line("Sleep", profile.sleep_minutes, blocked);
  t.body += blocked ? "\nScreen lock: not triggered by idle time" : "\nScreen lock: only when you ask for it";
  t.body += on_battery ? "\nPower profile: battery" : "\nPower profile: mains power";

  if (keep_awake || !holders.empty()) {
    t.body += "\n\nKept awake by:";
    if (keep_awake) t.body += "\n  - you (this padlock)";
    for (const LockHolder& h : holders) t.body += "\n  - " + describe_holder(h);
  }
  t.body += keep_awake ? "\n\nClick to let the screen turn off and the computer sleep again"
                       : "\n\nClick to keep the screen on and stop sleep";
  return t;
}

}  // namespace fleetwm
