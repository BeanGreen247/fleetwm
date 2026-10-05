#pragma once

#include <string>
#include <vector>

namespace fleetwm {

// The command line for the "open a terminal" shortcut. Plain `foot` is started with
// Fleetwm's bundled config (bigger font, the `~>` prompt) -- unless the user has their
// own ~/.config/foot/foot.ini, which always wins, or the bundled file is missing.
// Any other terminal command is run exactly as set.
inline std::vector<std::string> terminal_argv(const std::string& command, const std::string& sysconf_dir,
                                              bool user_foot_config_exists, bool bundled_foot_config_exists) {
  if (command == "foot" && !user_foot_config_exists && bundled_foot_config_exists)
    return {"foot", "-c", sysconf_dir + "/foot.ini"};
  return {command};
}

}  // namespace fleetwm
