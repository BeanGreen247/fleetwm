#pragma once

// Game launchers, stores, installers and a game's own settings window are not games even under Wine or with a Steam-style window class (Lutris gives every window
// steam_app_default, Heroic steam_app_0). Judged from a window title or a program's command line.

#include <cctype>
#include <string>

namespace fleetwm {

inline bool looks_like_launcher_text(std::string text) {
  static const char* const kWords[] = {"battle.net", "battlenet", "blizzard", "launcher", "epic games", "epicgames", "gog galaxy", "galaxyclient", "ubisoft connect", "upc.exe",
                                       "ea app", "eadesktop", "heroic", "lutris", "bottles", "playnite", "itch.io", "configuration", "setup", "installer", "updater", "patcher",
                                       "winecfg", "explorer.exe", "uninstall", "agent.exe", "crashreport", "bugreport"};
  for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (const char* w : kWords)
    if (text.find(w) != std::string::npos) return true;
  return false;
}

}  // namespace fleetwm
