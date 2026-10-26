#include "sessions.hpp"

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <dirent.h>

namespace fleetwm {

namespace {

std::string sess_trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
  return s.substr(b, e - b);
}

// Removes %f %u ... and turns %% into %.
std::string sess_strip_field_codes(const std::string& exec) {
  std::string out;
  for (size_t i = 0; i < exec.size(); ++i) {
    if (exec[i] != '%' || i + 1 >= exec.size()) {
      out += exec[i];
      continue;
    }
    ++i;
    if (exec[i] == '%') out += '%';
  }
  return sess_trim(out);
}

bool sess_executable_in_path(const std::string& name) {
  if (name.empty()) return true;
  if (name.find('/') != std::string::npos) return access(name.c_str(), X_OK) == 0;
  const char* path = std::getenv("PATH");
  std::stringstream ss(path != nullptr ? path : "/usr/local/bin:/usr/bin:/bin");
  std::string dir;
  while (std::getline(ss, dir, ':')) {
    if (!dir.empty() && access((dir + "/" + name).c_str(), X_OK) == 0) return true;
  }
  return false;
}

bool sess_ends_with(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

std::optional<SessionEntry> parse_session_desktop(const std::string& stem, SessionKind kind,
                                                  const std::string& text) {
  SessionEntry e;
  e.kind = kind;
  e.id = std::string(kind == SessionKind::Wayland ? "wayland:" : "x11:") + stem;
  std::string try_exec, names;
  bool in_entry = false, hidden = false;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    line = sess_trim(line);
    if (line.empty() || line[0] == '#') continue;
    if (line[0] == '[') {
      in_entry = line == "[Desktop Entry]";
      continue;
    }
    if (!in_entry) continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = sess_trim(line.substr(0, eq)), val = sess_trim(line.substr(eq + 1));
    if (key == "Name") e.name = val;
    else if (key == "Exec") e.exec = sess_strip_field_codes(val);
    else if (key == "TryExec") try_exec = val;
    else if (key == "DesktopNames") names = val;
    else if ((key == "Hidden" || key == "NoDisplay") && val == "true") hidden = true;
  }
  if (hidden || e.exec.empty()) return std::nullopt;
  if (!try_exec.empty() && !sess_executable_in_path(try_exec)) return std::nullopt;
  if (e.name.empty()) e.name = stem;
  std::replace(names.begin(), names.end(), ';', ':');
  while (!names.empty() && names.back() == ':') names.pop_back();
  e.desktop_names = names.empty() ? stem : names;
  return e;
}

std::vector<SessionEntry> discover_sessions(const std::vector<std::string>& wayland_dirs,
                                            const std::vector<std::string>& x11_dirs, bool allow_x11,
                                            const std::string& fleetwm_exec) {
  SessionEntry fleetwm;
  fleetwm.id = kFleetwmSessionId;
  fleetwm.name = "Fleetwm";
  fleetwm.exec = fleetwm_exec;
  fleetwm.desktop_names = "fleetwm";
  fleetwm.kind = SessionKind::Wayland;

  std::vector<SessionEntry> others;
  auto scan = [&](const std::vector<std::string>& dirs, SessionKind kind) {
    for (const std::string& dir : dirs) {
      DIR* d = opendir(dir.c_str());
      if (d == nullptr) continue;
      std::vector<std::string> files;
      while (const dirent* ent = readdir(d)) {
        const std::string fn = ent->d_name;
        if (sess_ends_with(fn, ".desktop")) files.push_back(fn);
      }
      closedir(d);
      std::sort(files.begin(), files.end());
      for (const std::string& fn : files) {
        std::ifstream f(dir + "/" + fn);
        std::stringstream buf;
        buf << f.rdbuf();
        auto e = parse_session_desktop(fn.substr(0, fn.size() - 8), kind, buf.str());
        if (!e || e->id == kFleetwmSessionId) continue;
        const bool dup = std::any_of(others.begin(), others.end(), [&](const SessionEntry& o) { return o.id == e->id; });
        if (!dup) others.push_back(*e);
      }
    }
  };
  scan(wayland_dirs, SessionKind::Wayland);
  if (allow_x11) scan(x11_dirs, SessionKind::X11);
  std::stable_sort(others.begin(), others.end(),
                   [](const SessionEntry& a, const SessionEntry& b) { return a.name < b.name; });
  others.insert(others.begin(), fleetwm);
  return others;
}

std::vector<std::string> default_wayland_session_dirs() {
  return {"/usr/local/share/wayland-sessions", "/usr/share/wayland-sessions"};
}

std::vector<std::string> default_x11_session_dirs() {
  return {"/usr/local/share/xsessions", "/usr/share/xsessions"};
}

bool x11_sessions_available() {
  return access("/usr/bin/Xorg", X_OK) == 0 && access("/usr/bin/xinit", X_OK) == 0;
}

const SessionEntry* find_session(const std::vector<SessionEntry>& list, const std::string& id) {
  for (const SessionEntry& s : list) {
    if (s.id == id) return &s;
  }
  return nullptr;
}

std::vector<std::string> session_argv(const SessionEntry& s, const std::string& fleetwm_exec, int x_display,
                                      int vt, bool have_xsession_wrapper) {
  if (s.id == kFleetwmSessionId) return {fleetwm_exec};
  if (s.kind == SessionKind::Wayland) return {"/bin/sh", "-c", "exec " + s.exec};
  std::vector<std::string> argv = {"/usr/bin/xinit"};
  if (have_xsession_wrapper) {
    argv.push_back("/etc/X11/Xsession");
    argv.push_back(s.exec);
  } else {
    argv.insert(argv.end(), {"/bin/sh", "-c", "exec " + s.exec});
  }
  argv.insert(argv.end(), {"--", "/usr/bin/Xorg", ":" + std::to_string(x_display)});
  if (vt > 0) argv.push_back("vt" + std::to_string(vt));
  argv.insert(argv.end(), {"-keeptty", "-noreset"});
  return argv;
}

int vt_from_tty(const std::string& tty_name) {
  if (tty_name.size() <= 3 || tty_name.compare(0, 3, "tty") != 0) return 0;
  int n = 0;
  for (size_t i = 3; i < tty_name.size(); ++i) {
    if (tty_name[i] < '0' || tty_name[i] > '9') return 0;
    n = n * 10 + (tty_name[i] - '0');
  }
  return n;
}

int first_free_x_display() {
  for (int n = 0; n < 64; ++n) {
    if (access(("/tmp/.X11-unix/X" + std::to_string(n)).c_str(), F_OK) != 0) return n;
  }
  return 0;
}

}  // namespace fleetwm
