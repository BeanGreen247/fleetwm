#include "autostart.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

#include "config_paths.hpp"

namespace fleetwm {

namespace fs = std::filesystem;

std::string autostart_dir() { return (config_internal::config_home() / "autostart").string(); }

namespace {

constexpr const char* kMarker = "X-Fleetwm-Autostart";

std::map<std::string, std::string> read_entry(const fs::path& path) {
  std::map<std::string, std::string> kv;
  std::ifstream f(path);
  std::string line;
  bool in_entry = false;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && line[0] == '[') {
      in_entry = line == "[Desktop Entry]";
      continue;
    }
    const size_t eq = line.find('=');
    if (!in_entry || eq == std::string::npos || line[0] == '#') continue;
    std::string key = line.substr(0, eq), val = line.substr(eq + 1);
    while (!key.empty() && key.back() == ' ') key.pop_back();
    while (!val.empty() && val.front() == ' ') val.erase(val.begin());
    if (!kv.count(key)) kv[key] = val;  // the first one wins
  }
  return kv;
}

bool is_enabled(const std::map<std::string, std::string>& kv) {
  auto get = [&](const char* k) { auto it = kv.find(k); return it == kv.end() ? std::string() : it->second; };
  return get("Type") == "Application" && !get("Exec").empty() && get("Hidden") != "true" && get("X-GNOME-Autostart-enabled") != "false";
}

}  // namespace

std::string autostart_file_text(const AutostartApp& app) {
  std::ostringstream o;
  o << "[Desktop Entry]\nType=Application\nName=" << app.name << "\nExec=" << app.exec << "\n";
  if (!app.icon.empty()) o << "Icon=" << app.icon << "\n";
  o << "Terminal=" << (app.terminal ? "true" : "false") << "\n" << kMarker << "=true\n";
  return o.str();
}

std::string autostart_command(const std::string& exec) {
  std::string out;
  for (size_t i = 0; i < exec.size(); ++i) {
    if (exec[i] != '%' || i + 1 >= exec.size()) {
      out += exec[i];
      continue;
    }
    if (exec[++i] == '%') out += '%';  // every other field code is dropped
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

std::set<std::string> autostart_enabled_ids() {
  std::set<std::string> ids;
  std::error_code ec;
  for (const fs::directory_entry& e : fs::directory_iterator(autostart_dir(), ec))
    if (e.path().extension() == ".desktop" && is_enabled(read_entry(e.path()))) ids.insert(e.path().filename().string());
  return ids;
}

std::vector<std::string> autostart_commands() {
  std::vector<fs::path> files;
  std::error_code ec;
  for (const fs::directory_entry& e : fs::directory_iterator(autostart_dir(), ec))
    if (e.path().extension() == ".desktop") files.push_back(e.path());
  std::sort(files.begin(), files.end());
  std::vector<std::string> out;
  for (const fs::path& f : files) {
    const auto kv = read_entry(f);
    if (!is_enabled(kv)) continue;
    const std::string cmd = autostart_command(kv.at("Exec"));
    if (!cmd.empty()) out.push_back(cmd);
  }
  return out;
}

void autostart_set(const AutostartApp& app, bool on) {
  const fs::path path = fs::path(autostart_dir()) / app.id;
  std::error_code ec;
  const bool exists = fs::exists(path, ec);
  if (on) {
    if (exists) {
      // somebody else's file that is merely switched off: take Hidden=true out again instead of replacing the file
      auto kv = read_entry(path);
      if (!kv.count(kMarker)) {
        std::ifstream in(path);
        std::string text, line;
        while (std::getline(in, line))
          if (line.rfind("Hidden=", 0) != 0 && line.rfind("X-GNOME-Autostart-enabled=", 0) != 0) text += line + "\n";
        std::ofstream out(path);
        if (!out) throw std::runtime_error("cannot write " + path.string());
        out << text;
        return;
      }
    }
    fs::create_directories(path.parent_path());
    const fs::path tmp = path.string() + ".tmp";
    {
      std::ofstream out(tmp);
      if (!out) throw std::runtime_error("cannot write " + tmp.string());
      out << autostart_file_text(app);
      if (!out) throw std::runtime_error("cannot write " + tmp.string());
    }
    fs::rename(tmp, path, ec);
    if (ec) throw std::runtime_error("cannot save " + path.string() + ": " + ec.message());
    return;
  }
  if (!exists) return;
  if (read_entry(path).count(kMarker)) {
    fs::remove(path, ec);
    return;
  }
  // put Hidden=true right under the [Desktop Entry] header, so it cannot land in another group
  std::string text, line;
  {
    std::ifstream in(path);
    while (std::getline(in, line)) {
      if (line.rfind("Hidden=", 0) == 0) continue;
      text += line + "\n";
      if (line == "[Desktop Entry]") text += "Hidden=true\n";
    }
  }
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write " + path.string());
  out << text;
}

}  // namespace fleetwm
