#pragma once

// The Steam games installed on this computer, and TuxThrottle's per-game switches for them. TuxThrottle puts a gate script ahead of `mangohud` on PATH:
// a Steam game launched with `mangohud %command%` only gets the overlay when ~/.config/tuxthrottle/mangohud-games/<appid>.on exists, and runs on the
// dedicated graphics card when <appid>.dgpu exists. Without the switch the launch option is silently ignored, which looks like Steam disregarding the
// MangoHud settings. Settings -> Gaming lists the games with both switches so nobody has to open TuxThrottle for that.

#include <string>
#include <vector>

#include "game_config.hpp"

namespace fleetwm {

struct SteamGame {
  std::string appid;
  std::string name;
};

// Pure parts (tested): the "appid" and "name" of an appmanifest_*.acf, and the library folders in libraryfolders.vdf. {} / "" when absent.
SteamGame parse_steam_manifest(const std::string& text);
std::vector<std::string> parse_steam_library_paths(const std::string& vdf_text);
// True for the entries that are not games: Proton versions, Steam Linux Runtime, Steamworks redistributables, the Steam Deck controller configs.
bool is_steam_tool(const std::string& name);

// Reads every library listed in <steam_root>/steamapps/libraryfolders.vdf (default roots: ~/.local/share/Steam, ~/.steam/steam), games sorted by name.
std::vector<SteamGame> installed_steam_games();

// TuxThrottle's per-game switches. `dir` defaults to ~/.config/tuxthrottle/mangohud-games.
enum class GameSwitch { MangoHud, DedicatedGpu };
bool tuxthrottle_mangohud_gate_installed();  // the gate script ahead of mangohud on PATH exists
bool game_switch_get(const std::string& appid, GameSwitch which, const std::string& dir = "");
bool game_switch_set(const std::string& appid, GameSwitch which, bool on, const std::string& dir = "");

// "All games on one GPU", kept in step with TuxThrottle in both directions. TuxThrottle's switch is the per-game <appid>.dgpu file (present: the dedicated GPU, absent:
// the integrated one), so the overall choice is read from those files: all present is Dedicated, none present is Integrated, a mix is None.
AllGamesGpu all_games_gpu_from_switches(const std::vector<bool>& dedicated_flags);
// Writes the choice to every game's .dgpu switch (None changes nothing). Returns false when a file could not be written.
bool apply_all_games_gpu(const std::vector<SteamGame>& games, AllGamesGpu mode, const std::string& dir = "");
// Reconciles `config` with TuxThrottle's switches at `now` (unix seconds). The newer side wins: a TuxThrottle switch changed after the last change made here
// (file times, with 2 s of slack) is read into the config; otherwise the config is written to TuxThrottle again, which also covers games installed since.
// Returns true when `config` changed and has to be saved. Does nothing without games or without the gate directory's TuxThrottle.
bool sync_all_games_gpu(GameConfig* config, const std::vector<SteamGame>& games, long long now, const std::string& dir = "");

}  // namespace fleetwm
