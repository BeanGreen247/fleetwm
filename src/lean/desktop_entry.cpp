#include "desktop_entry.hpp"
#include "util.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fleetwm::lean {

namespace {

using detail::split;
using detail::trim;

// Desktop-spec string unescape: \s \n \t \r \\ (and \; in lists, left alone).
std::string unescape(const std::string& v) {
  std::string out;
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] == '\\' && i + 1 < v.size()) {
      switch (v[++i]) {
        case 's': out += ' '; break;
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case '\\': out += '\\'; break;
        default: out += '\\'; out += v[i]; break;
      }
    } else {
      out += v[i];
    }
  }
  return out;
}

// Locale suffixes to try for Name[...] in priority order: lang_COUNTRY, lang.
std::vector<std::string> locale_keys() {
  const char* l = std::getenv("LC_MESSAGES");
  if (!l || !*l) l = std::getenv("LANG");
  std::vector<std::string> out;
  if (!l || !*l) return out;
  std::string loc = l;
  const size_t dot = loc.find('.');
  if (dot != std::string::npos) loc.resize(dot);
  const size_t at = loc.find('@');
  if (at != std::string::npos) loc.resize(at);
  if (loc == "C" || loc == "POSIX") return out;
  out.push_back(loc);
  const size_t us = loc.find('_');
  if (us != std::string::npos) out.push_back(loc.substr(0, us));
  return out;
}

bool on_path(const std::string& prog) {
  if (prog.empty()) return false;
  if (prog[0] == '/') return access(prog.c_str(), X_OK) == 0;
  const char* path = std::getenv("PATH");
  for (const auto& d : split(path ? path : "/usr/local/bin:/usr/bin:/bin", ':'))
    if (access((d + "/" + prog).c_str(), X_OK) == 0) return true;
  return false;
}

bool parse_file(const std::string& file, const std::string& id, DesktopEntry* out) {
  std::ifstream in(file);
  if (!in) return false;
  const auto locales = locale_keys();
  std::string line;
  bool in_entry = false;
  std::map<std::string, std::string> kv;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    if (line[0] == '[') {
      if (in_entry) break;  // only [Desktop Entry]; actions come after
      in_entry = line == "[Desktop Entry]";
      continue;
    }
    if (!in_entry) continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    kv[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
  }
  if (kv["Type"] != "Application") return false;
  if (kv["NoDisplay"] == "true" || kv["Hidden"] == "true") return false;

  const char* cd = std::getenv("XDG_CURRENT_DESKTOP");
  const auto desktops = split(cd ? cd : "", ':');
  auto matches = [&](const std::string& list) {
    for (const auto& d : split(list, ';'))
      if (std::find(desktops.begin(), desktops.end(), d) != desktops.end()) return true;
    return false;
  };
  if (kv.count("OnlyShowIn") && !matches(kv["OnlyShowIn"])) return false;
  if (kv.count("NotShowIn") && matches(kv["NotShowIn"])) return false;
  if (kv.count("TryExec") && !on_path(kv["TryExec"])) return false;

  auto localized = [&](const std::string& key) {
    for (const auto& l : locales) {
      auto it = kv.find(key + "[" + l + "]");
      if (it != kv.end()) return unescape(it->second);
    }
    auto it = kv.find(key);
    return it == kv.end() ? std::string() : unescape(it->second);
  };
  out->id = id;
  out->file_path = file;
  out->name = localized("Name");
  out->comment = localized("Comment");
  out->exec = kv["Exec"];
  out->icon = kv["Icon"];
  out->categories = kv["Categories"];
  out->path = kv["Path"];
  out->terminal = kv["Terminal"] == "true";
  return !out->name.empty() && !out->exec.empty();
}

void scan_dir(const std::string& base, const std::string& rel, std::set<std::string>* seen,
              std::vector<DesktopEntry>* out) {
  const std::string dir = rel.empty() ? base : base + "/" + rel;
  DIR* d = opendir(dir.c_str());
  if (!d) return;
  while (dirent* e = readdir(d)) {
    if (e->d_name[0] == '.') continue;
    const std::string name = e->d_name;
    const std::string full = dir + "/" + name;
    struct stat st;
    if (stat(full.c_str(), &st) != 0) continue;
    const std::string rel_name = rel.empty() ? name : rel + "-" + name;  // subdir/foo.desktop -> subdir-foo.desktop
    if (S_ISDIR(st.st_mode)) {
      scan_dir(base, rel.empty() ? name : rel + "/" + name, seen, out);
      continue;
    }
    if (name.size() < 9 || name.compare(name.size() - 8, 8, ".desktop") != 0) continue;
    std::string id = rel.empty() ? name : rel;
    for (auto& c : id) if (c == '/') c = '-';
    if (!rel.empty()) id += "-" + name;
    if (!seen->insert(id).second) continue;  // earlier (higher priority) dir wins
    DesktopEntry entry;
    if (parse_file(full, id, &entry)) out->push_back(std::move(entry));
    (void)rel_name;
  }
  closedir(d);
}

}  // namespace

std::vector<DesktopEntry> load_desktop_entries() {
  std::vector<std::string> dirs;
  const char* xdh = std::getenv("XDG_DATA_HOME");
  const char* home = std::getenv("HOME");
  if (xdh && *xdh) dirs.push_back(std::string(xdh) + "/applications");
  else if (home) dirs.push_back(std::string(home) + "/.local/share/applications");
  const char* xdd = std::getenv("XDG_DATA_DIRS");
  for (const auto& d : split(xdd && *xdd ? xdd : "/usr/local/share:/usr/share", ':'))
    dirs.push_back(d + "/applications");
  std::set<std::string> seen;
  std::vector<DesktopEntry> out;
  for (const auto& d : dirs) scan_dir(d, "", &seen, &out);
  return out;
}

std::vector<std::string> exec_argv(const DesktopEntry& e) {
  // 1. Tokenize per the Exec quoting rules (double quotes, backslash escapes inside them).
  std::vector<std::string> tokens;
  std::string cur;
  bool have = false, quoted = false;
  for (size_t i = 0; i < e.exec.size(); ++i) {
    const char c = e.exec[i];
    if (quoted) {
      if (c == '\\' && i + 1 < e.exec.size() && std::strchr("\"`$\\", e.exec[i + 1])) {
        cur += e.exec[++i];
      } else if (c == '"') {
        quoted = false;
      } else {
        cur += c;
      }
    } else if (c == '"') {
      quoted = true;
      have = true;
    } else if (c == ' ' || c == '\t') {
      if (have || !cur.empty()) tokens.push_back(cur);
      cur.clear();
      have = false;
    } else {
      cur += c;
      have = true;
    }
  }
  if (have || !cur.empty()) tokens.push_back(cur);

  // 2. Field codes.
  std::vector<std::string> argv;
  for (const auto& t : tokens) {
    if (t == "%i") {
      if (!e.icon.empty()) {
        argv.push_back("--icon");
        argv.push_back(e.icon);
      }
      continue;
    }
    std::string out;
    for (size_t i = 0; i < t.size(); ++i) {
      if (t[i] != '%' || i + 1 >= t.size()) {
        out += t[i];
        continue;
      }
      const char code = t[++i];
      switch (code) {
        case '%': out += '%'; break;
        case 'c': out += e.name; break;
        case 'k': out += e.file_path; break;
        default: break;  // %f %F %u %U %d %D %n %N %v %m and unknown: dropped
      }
    }
    if (!out.empty() || t.find('%') == std::string::npos) argv.push_back(out);
  }
  return argv;
}

std::string exec_basename(const DesktopEntry& e) {
  const auto argv = exec_argv(e);
  if (argv.empty()) return "";
  const size_t slash = argv[0].rfind('/');
  return slash == std::string::npos ? argv[0] : argv[0].substr(slash + 1);
}

bool spawn_detached(const std::vector<std::string>& argv, const std::string& workdir) {
  if (argv.empty()) return false;
  std::vector<char*> args;
  for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
  args.push_back(nullptr);
  const pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    setsid();
    if (!workdir.empty() && chdir(workdir.c_str()) != 0) {
    }
    const int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
      dup2(devnull, STDIN_FILENO);
      dup2(devnull, STDOUT_FILENO);
      dup2(devnull, STDERR_FILENO);
      if (devnull > 2) close(devnull);
    }
    execvp(args[0], args.data());
    _exit(127);
  }
  return true;
}

}  // namespace fleetwm::lean
