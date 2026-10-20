#include "child_signals.hpp"
#include "app_appearance.hpp"

#include <spawn.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "config_paths.hpp"

extern char** environ;

namespace fleetwm {

namespace fs = std::filesystem;

std::string merge_gtk_settings(const std::string& existing, bool dark) {
  const std::string line = std::string("gtk-application-prefer-dark-theme=") + (dark ? "true" : "false");
  std::vector<std::string> lines;
  {
    std::istringstream in(existing);
    for (std::string l; std::getline(in, l);) lines.push_back(l);
  }
  auto is_section = [](const std::string& l) { return !l.empty() && l.front() == '['; };
  auto is_key = [](const std::string& l) {
    const size_t eq = l.find('=');
    if (eq == std::string::npos) return false;
    std::string key = l.substr(0, eq);
    key.erase(key.find_last_not_of(" \t") == std::string::npos ? 0 : key.find_last_not_of(" \t") + 1);
    return key == "gtk-application-prefer-dark-theme";
  };

  bool in_settings = false, done = false;
  size_t settings_end = lines.size();  // where a new key goes: right after the last line of [Settings]
  bool have_settings = false;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (is_section(lines[i])) {
      if (in_settings && !done) settings_end = i;
      in_settings = lines[i].rfind("[Settings]", 0) == 0;
      if (in_settings) have_settings = true;
      continue;
    }
    if (in_settings && is_key(lines[i])) {
      lines[i] = line;
      done = true;
    }
  }
  if (!done) {
    if (!have_settings) {
      if (!lines.empty() && !lines.back().empty()) lines.push_back("");
      lines.push_back("[Settings]");
      lines.push_back(line);
    } else {
      // Skip back over blank lines so the key stays with its section.
      size_t at = settings_end;
      while (at > 0 && lines[at - 1].empty() && at > 1) --at;
      lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), line);
    }
  }
  std::string out;
  for (const std::string& l : lines) out += l + "\n";
  return out;
}

std::string appearance_shell_script(bool dark) {
  const char* scheme = dark ? "prefer-dark" : "prefer-light";
  const char* theme = dark ? "Adwaita-dark" : "Adwaita";
  std::string s;
  s += "command -v gsettings >/dev/null 2>&1 || exit 0\n";
  s += std::string("gsettings set org.gnome.desktop.interface color-scheme '") + scheme + "'\n";
  s += "cur=$(gsettings get org.gnome.desktop.interface gtk-theme 2>/dev/null)\n";
  s += "case \"$cur\" in \"'Adwaita'\"|\"'Adwaita-dark'\"|\"''\"|\"\") ";
  s += std::string("gsettings set org.gnome.desktop.interface gtk-theme '") + theme + "' ;; esac\n";
  return s;
}

namespace {

void write_gtk_settings_file(const fs::path& dir, bool dark) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  const fs::path file = dir / "settings.ini";
  std::string existing;
  if (std::ifstream in(file); in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    existing = ss.str();
  }
  const std::string merged = merge_gtk_settings(existing, dark);
  if (merged == existing) return;  // already right: do not touch the file
  std::ofstream out(file, std::ios::trunc);
  if (out) out << merged;
}

}  // namespace

void apply_app_appearance(bool dark) {
  const fs::path base = config_internal::config_home();
  write_gtk_settings_file(base / "gtk-3.0", dark);
  write_gtk_settings_file(base / "gtk-4.0", dark);

  const std::string script = appearance_shell_script(dark);
  const char* argv[] = {"sh", "-c", script.c_str(), nullptr};
  pid_t pid;
  posix_spawn(&pid, "/bin/sh", nullptr, CleanSpawnAttr().get(), const_cast<char* const*>(argv), environ);
}

std::string set_custom_chrome_frame(const std::string& preferences, bool custom) {
  static const std::string key = "\"custom_chrome_frame\":";
  const size_t at = preferences.find(key);
  if (at == std::string::npos) return preferences;
  size_t v = at + key.size();
  while (v < preferences.size() && preferences[v] == ' ') ++v;
  size_t end = v;
  while (end < preferences.size() && std::isalpha(static_cast<unsigned char>(preferences[end]))) ++end;
  const std::string current = preferences.substr(v, end - v);
  if (current != "true" && current != "false") return preferences;
  std::string out = preferences;
  out.replace(v, end - v, custom ? "true" : "false");
  return out;
}

namespace {

// A browser's config directory holds SingletonLock (a symlink to "host-pid") while it runs.
bool browser_running(const fs::path& dir) {
  std::error_code ec;
  return fs::is_symlink(dir / "SingletonLock", ec);
}

void set_profile_frame(const fs::path& file, bool custom) {
  std::string text;
  if (std::ifstream in(file, std::ios::binary); in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    text = ss.str();
  } else {
    return;
  }
  const std::string next = set_custom_chrome_frame(text, custom);
  if (next == text) return;
  const fs::path tmp = file.string() + ".fleetwm";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << next;
    if (!out) return;
  }
  std::error_code ec;
  fs::rename(tmp, file, ec);
  if (ec) fs::remove(tmp, ec);
}

}  // namespace

void apply_browser_frames(bool compositor_draws_titlebars) {
  const fs::path base = config_internal::config_home();
  static const char* const browsers[] = {"BraveSoftware/Brave-Browser", "BraveSoftware/Brave-Browser-Beta",
                                         "google-chrome", "google-chrome-beta", "chromium",
                                         "microsoft-edge", "vivaldi"};
  std::error_code ec;
  for (const char* name : browsers) {
    const fs::path dir = base / name;
    if (!fs::is_directory(dir, ec) || browser_running(dir)) continue;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
      const std::string leaf = entry.path().filename().string();
      if (leaf == "Default" || leaf.rfind("Profile ", 0) == 0) {
        set_profile_frame(entry.path() / "Preferences", !compositor_draws_titlebars);
      }
    }
  }
}

}  // namespace fleetwm
