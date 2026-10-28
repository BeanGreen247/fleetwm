#include "steam_games.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

namespace fleetwm {

namespace {
// The value of the line `"key"  "value"` (any whitespace between), "" when there is none.
std::string vdf_value(const std::string& text, const std::string& key, size_t from = 0, size_t* end = nullptr) {
  const std::string quoted = "\"" + key + "\"";
  for (size_t at = text.find(quoted, from); at != std::string::npos; at = text.find(quoted, at + 1)) {
    size_t i = at + quoted.size();
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
    if (i >= text.size() || text[i] != '"') continue;
    const size_t close = text.find('"', i + 1);
    if (close == std::string::npos) return "";
    if (end) *end = close + 1;
    return text.substr(i + 1, close - i - 1);
  }
  return "";
}

std::string default_gate_dir() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  const char* home = std::getenv("HOME");
  const std::string base = xdg && *xdg ? xdg : (home ? std::string(home) + "/.config" : "");
  return base + "/tuxthrottle/mangohud-games";
}

std::string switch_path(const std::string& appid, GameSwitch which, const std::string& dir) {
  return (dir.empty() ? default_gate_dir() : dir) + "/" + appid + (which == GameSwitch::MangoHud ? ".on" : ".dgpu");
}

bool valid_appid(const std::string& id) {
  return !id.empty() && id.size() < 12 && std::all_of(id.begin(), id.end(), [](char c) { return c >= '0' && c <= '9'; });
}
}  // namespace

SteamGame parse_steam_manifest(const std::string& text) {
  SteamGame g;
  g.appid = vdf_value(text, "appid");
  g.name = vdf_value(text, "name");
  if (!valid_appid(g.appid)) return {};
  return g;
}

std::vector<std::string> parse_steam_library_paths(const std::string& vdf_text) {
  std::vector<std::string> out;
  size_t at = 0;
  for (;;) {
    size_t end = 0;
    const std::string p = vdf_value(vdf_text, "path", at, &end);
    if (end == 0) break;
    if (!p.empty()) out.push_back(p);
    at = end;
  }
  return out;
}

bool is_steam_tool(const std::string& name) {
  for (const char* prefix : {"Proton ", "Steam Linux Runtime", "Steamworks", "Steam Controller Configs", "Steam.dll", "Steam Deck"})
    if (name.rfind(prefix, 0) == 0) return true;
  return false;
}

std::vector<SteamGame> installed_steam_games() {
  std::vector<std::string> roots;
  if (const char* home = std::getenv("HOME")) {
    roots.push_back(std::string(home) + "/.local/share/Steam");
    roots.push_back(std::string(home) + "/.steam/steam");
  }
  std::set<std::string> libraries;
  auto slurp = [](const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
  };
  for (const std::string& root : roots) {
    libraries.insert(root);
    for (const std::string& p : parse_steam_library_paths(slurp(root + "/steamapps/libraryfolders.vdf"))) libraries.insert(p);
  }
  std::vector<SteamGame> games;
  std::set<std::string> seen;
  for (const std::string& lib : libraries) {
    const std::string dir = lib + "/steamapps";
    if (DIR* d = opendir(dir.c_str())) {
      while (dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name.rfind("appmanifest_", 0) != 0 || name.size() < 17 || name.compare(name.size() - 4, 4, ".acf") != 0) continue;
        const SteamGame g = parse_steam_manifest(slurp(dir + "/" + name));
        if (g.appid.empty() || g.name.empty() || is_steam_tool(g.name) || !seen.insert(g.appid).second) continue;
        games.push_back(g);
      }
      closedir(d);
    }
  }
  std::sort(games.begin(), games.end(), [](const SteamGame& a, const SteamGame& b) {
    std::string x = a.name, y = b.name;
    for (char& c : x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : y) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return x < y;
  });
  return games;
}

bool tuxthrottle_mangohud_gate_installed() {
  std::ifstream in("/usr/local/bin/mangohud");
  std::string head;
  for (int i = 0; i < 4 && std::getline(in, head); ++i)
    if (head.find("tuxthrottle-mangohud-gate") != std::string::npos) return true;
  return false;
}

bool game_switch_get(const std::string& appid, GameSwitch which, const std::string& dir) {
  return valid_appid(appid) && ::access(switch_path(appid, which, dir).c_str(), F_OK) == 0;
}

bool game_switch_set(const std::string& appid, GameSwitch which, bool on, const std::string& dir) {
  if (!valid_appid(appid)) return false;
  const std::string path = switch_path(appid, which, dir);
  if (!on) return ::unlink(path.c_str()) == 0 || errno == ENOENT;
  const std::string folder = path.substr(0, path.rfind('/'));
  ::mkdir(folder.substr(0, folder.rfind('/')).c_str(), 0755);  // ~/.config/tuxthrottle
  ::mkdir(folder.c_str(), 0755);
  std::ofstream out(path);
  return static_cast<bool>(out);
}

}  // namespace fleetwm
