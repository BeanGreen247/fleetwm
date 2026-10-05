#include "mimeapps.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

#include "util.hpp"

namespace fleetwm::kit {

namespace {

using detail::split;
using detail::trim;

// section -> (mime -> list of ids)
using IniMap = std::map<std::string, std::map<std::string, std::vector<std::string>>>;

IniMap parse_ini(const std::string& path) {
  IniMap out;
  std::ifstream in(path);
  std::string line, section;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    if (line[0] == '[') {
      section = line.substr(1, line.find(']') - 1);
      continue;
    }
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    out[section][trim(line.substr(0, eq))] = split(line.substr(eq + 1), ';');
  }
  return out;
}

std::string home() {
  const char* h = std::getenv("HOME");
  return h ? h : "";
}

std::string config_home() {
  const char* x = std::getenv("XDG_CONFIG_HOME");
  return x && *x ? x : home() + "/.config";
}

std::string data_home() {
  const char* x = std::getenv("XDG_DATA_HOME");
  return x && *x ? x : home() + "/.local/share";
}

std::vector<std::string> data_dirs() {
  const char* x = std::getenv("XDG_DATA_DIRS");
  return split(x && *x ? x : "/usr/local/share:/usr/share", ':');
}

std::vector<std::string> config_dirs() {
  const char* x = std::getenv("XDG_CONFIG_DIRS");
  return split(x && *x ? x : "/etc/xdg", ':');
}

// mimeapps.list files, highest priority first.
std::vector<std::string> list_files() {
  std::vector<std::string> f{config_home() + "/mimeapps.list", data_home() + "/applications/mimeapps.list"};
  for (const auto& d : config_dirs()) f.push_back(d + "/mimeapps.list");
  for (const auto& d : data_dirs()) f.push_back(d + "/applications/mimeapps.list");
  for (const auto& d : data_dirs()) f.push_back(d + "/applications/defaults.list");
  return f;
}

std::string user_list() { return config_home() + "/mimeapps.list"; }

void push_unique(std::vector<std::string>* v, const std::string& s) {
  if (std::find(v->begin(), v->end(), s) == v->end()) v->push_back(s);
}

}  // namespace

std::vector<std::string> mime_apps_for(const std::string& mime) {
  std::vector<std::string> out, removed;
  for (const auto& f : list_files()) {
    IniMap ini = parse_ini(f);
    for (const auto& id : ini["Removed Associations"][mime]) removed.push_back(id);
    for (const auto& id : ini["Added Associations"][mime]) push_unique(&out, id);
  }
  std::vector<std::string> caches{data_home() + "/applications/mimeinfo.cache"};
  for (const auto& d : data_dirs()) caches.push_back(d + "/applications/mimeinfo.cache");
  for (const auto& c : caches) {
    IniMap ini = parse_ini(c);
    for (const auto& id : ini["MIME Cache"][mime]) push_unique(&out, id);
  }
  out.erase(std::remove_if(out.begin(), out.end(),
                           [&](const std::string& id) {
                             return std::find(removed.begin(), removed.end(), id) != removed.end();
                           }),
            out.end());
  return out;
}

std::string mime_default_for(const std::string& mime) {
  const auto assoc = mime_apps_for(mime);
  for (const auto& f : list_files()) {
    IniMap ini = parse_ini(f);
    const auto& defaults = ini["Default Applications"][mime];
    for (const auto& id : defaults)
      if (std::find(assoc.begin(), assoc.end(), id) != assoc.end()) return id;
  }
  return assoc.empty() ? "" : assoc.front();
}

bool mime_set_default(const std::string& mime, const std::string& desktop_id) {
  const std::string path = user_list();
  std::vector<std::string> lines;
  {
    std::ifstream in(path);
    std::string l;
    while (std::getline(in, l)) lines.push_back(l);
  }
  auto set_in_section = [&](const std::string& section, const std::string& value_line, bool prepend_id) {
    const std::string header = "[" + section + "]";
    size_t start = std::string::npos, end = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) {
      if (trim(lines[i]) == header) {
        start = i;
        end = lines.size();
        for (size_t j = i + 1; j < lines.size(); ++j)
          if (!trim(lines[j]).empty() && trim(lines[j])[0] == '[') {
            end = j;
            break;
          }
        break;
      }
    }
    if (start == std::string::npos) {
      if (!lines.empty() && !trim(lines.back()).empty()) lines.push_back("");
      lines.push_back(header);
      lines.push_back(value_line);
      return;
    }
    for (size_t i = start + 1; i < end; ++i) {
      const std::string t = trim(lines[i]);
      if (t.rfind(mime + "=", 0) == 0) {
        if (prepend_id) {
          const auto ids = split(t.substr(mime.size() + 1), ';');
          std::string v = desktop_id + ";";
          for (const auto& id : ids)
            if (id != desktop_id) v += id + ";";
          lines[i] = mime + "=" + v;
        } else {
          lines[i] = value_line;
        }
        return;
      }
    }
    lines.insert(lines.begin() + static_cast<long>(end), value_line);
  };
  set_in_section("Default Applications", mime + "=" + desktop_id + ";", false);
  set_in_section("Added Associations", mime + "=" + desktop_id + ";", true);

  std::string body;
  for (const auto& l : lines) body += l + "\n";
  const std::string tmp = path + ".tmp";
  {
    const std::string dir = path.substr(0, path.rfind('/'));
    mkdir(dir.c_str(), 0755);
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) return false;
    out << body;
    if (!out) return false;
  }
  return std::rename(tmp.c_str(), path.c_str()) == 0;
}

}  // namespace fleetwm::kit
