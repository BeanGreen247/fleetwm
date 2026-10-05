#include "app_appearance.hpp"

#include <spawn.h>

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
  posix_spawn(&pid, "/bin/sh", nullptr, nullptr, const_cast<char* const*>(argv), environ);
}

}  // namespace fleetwm
