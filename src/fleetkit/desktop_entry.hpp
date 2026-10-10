#pragma once

// freedesktop .desktop file scanning and Exec expansion, GTK/GLib-free
// (replaces GDesktopAppInfo for the launcher).

#include <string>
#include <vector>

namespace fleetwm::kit {

struct DesktopEntry {
  std::string id;          // e.g. "org.gnome.Nautilus.desktop"
  std::string file_path;
  std::string name;        // localized Name=
  std::string comment;     // localized Comment=
  std::string exec;        // raw Exec=
  std::string icon;
  std::string startup_wm_class;  // StartupWMClass used by the compositor app id
  std::string categories;  // raw Categories= ("A;B;")
  std::string path;        // Path= working directory
  bool terminal = false;
};

// All application entries that should be shown in a launcher: Type=Application,
// not NoDisplay/Hidden, OnlyShowIn/NotShowIn honoured against
// $XDG_CURRENT_DESKTOP, TryExec resolvable. User entries override system
// ones with the same id. Unsorted.
std::vector<DesktopEntry> load_desktop_entries();

// Built-in Fleetwm applications remain discoverable when an older install is
// missing one of their desktop files. The loader only adds entries whose
// executable is available on PATH.
std::vector<DesktopEntry> fleetwm_builtin_desktop_entries();

// Expands the Exec= line into an argv (field codes removed/substituted per the
// spec: %f %F %u %U ... dropped, %i -> --icon X, %c -> name, %k -> file, %% -> %).
std::vector<std::string> exec_argv(const DesktopEntry& e);

// Basename of the program in Exec= (first argv element), or "".
std::string exec_basename(const DesktopEntry& e);

// Starts argv detached (new session, stdio to /dev/null) in `workdir` if
// non-empty. Returns false if the fork failed.
bool spawn_detached(const std::vector<std::string>& argv, const std::string& workdir = "");

}  // namespace fleetwm::kit
