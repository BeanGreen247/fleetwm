#pragma once

#include <set>
#include <string>
#include <vector>

// Programs that start with the session, picked in Settings -> Startup Apps. They are standard XDG autostart entries in ~/.config/autostart/, so other desktops
// and tools see the same list. The compositor starts the enabled ones after its own helpers (src/compositor/server.cpp).
//   * An entry this module writes carries `X-Fleetwm-Autostart=true`; turning it off deletes the file.
//   * An entry somebody else wrote (no marker) is never deleted: turning it off adds `Hidden=true`, which every desktop honours.
//   * An entry counts as off when it has Hidden=true or X-GNOME-Autostart-enabled=false.

namespace fleetwm {

std::string autostart_dir();

struct AutostartApp {
  std::string id;    // the desktop file name, "firefox.desktop"
  std::string name;
  std::string exec;  // raw Exec= line
  std::string icon;
  bool terminal = false;
};

// The text of the autostart file for `app`.
std::string autostart_file_text(const AutostartApp& app);
// The program to run for an Exec= line: field codes (%f %U ...) dropped, %% kept as %.
std::string autostart_command(const std::string& exec);

// Ids of the enabled entries in the autostart directory, and their commands in file-name order.
std::set<std::string> autostart_enabled_ids();
std::vector<std::string> autostart_commands();

// Turns `app` on or off as described above. Throws std::runtime_error on I/O failure.
void autostart_set(const AutostartApp& app, bool on);

}  // namespace fleetwm
