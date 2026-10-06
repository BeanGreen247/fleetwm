#pragma once

// What each power menu entry runs, and which polkit actions it needs. Header-only so the unit tests can
// check it without a display: the menu has no password prompt (Fleetwm has no polkit agent), so every
// action must be one that `systemd-logind` allows an active local session to do on its own, and the
// installer ships packaging/50-fleetwm-power.rules to make sure that holds on every distribution.

#include <string>
#include <vector>

namespace fleetwm::power {

enum Action { kLock, kLogout, kSleep, kReboot, kShutdown, kCount };

inline const char* label(int a) {
  static const char* const kLabels[kCount] = {"Lock", "Log out", "Sleep", "Reboot", "Shut down"};
  return a >= 0 && a < kCount ? kLabels[a] : "";
}

// The command a button runs (argv). Lock goes through the compositor and has no command. Log out
// needs the id of the session to end, and gives an empty command without one.
inline std::vector<std::string> command_for(int a, const std::string& session_id = "") {
  switch (a) {
    case kLogout:
      return session_id.empty() ? std::vector<std::string>{}
                                : std::vector<std::string>{"loginctl", "terminate-session", session_id};
    case kSleep: return {"systemctl", "suspend"};
    case kReboot: return {"systemctl", "reboot"};
    case kShutdown: return {"systemctl", "poweroff"};
    default: return {};
  }
}

// The polkit actions logind checks for that command: the plain one, and the "other users are logged in"
// variant that otherwise asks an administrator for a password. The `-ignore-inhibit` variants are left
// out on purpose: a program holding an inhibitor (the keep-awake padlock) must still be respected.
inline std::vector<std::string> polkit_actions_for(int a) {
  const char* base = nullptr;
  switch (a) {
    case kSleep: base = "suspend"; break;
    case kReboot: base = "reboot"; break;
    case kShutdown: base = "power-off"; break;
    default: return {};
  }
  const std::string id = std::string("org.freedesktop.login1.") + base;
  return {id, id + "-multiple-sessions"};
}

}  // namespace fleetwm::power
