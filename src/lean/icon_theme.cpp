#include "icon_theme.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>

#include "image.hpp"
#include "util.hpp"

namespace fleetwm::lean {

namespace {

using detail::split;
using detail::trim;

bool file_exists(const std::string& p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::vector<std::string> icon_roots() {
  std::vector<std::string> roots;
  const char* home = std::getenv("HOME");
  if (home) roots.push_back(std::string(home) + "/.icons");
  const char* xdh = std::getenv("XDG_DATA_HOME");
  if (xdh && *xdh) roots.push_back(std::string(xdh) + "/icons");
  else if (home) roots.push_back(std::string(home) + "/.local/share/icons");
  const char* xdd = std::getenv("XDG_DATA_DIRS");
  for (const auto& d : split(xdd && *xdd ? xdd : "/usr/local/share:/usr/share", ':'))
    roots.push_back(d + "/icons");
  return roots;
}

struct Dir {
  std::string path;  // relative to the theme root, e.g. "48x48/apps"
  int size = 0, min_size = 0, max_size = 0, threshold = 2;
  char type = 'T';  // F ixed, S calable, T hreshold
};

struct Theme {
  std::string name;
  std::vector<std::string> roots;  // roots that contain <root>/<name>/
  std::vector<std::string> inherits;
  std::vector<Dir> dirs;
};

std::unique_ptr<Theme> parse_theme(const std::string& name) {
  auto theme = std::make_unique<Theme>();
  theme->name = name;
  std::string index_path;
  for (const auto& r : icon_roots()) {
    if (file_exists(r + "/" + name + "/index.theme")) {
      theme->roots.push_back(r);
      if (index_path.empty()) index_path = r + "/" + name + "/index.theme";
    } else {
      struct stat st;
      if (stat((r + "/" + name).c_str(), &st) == 0 && S_ISDIR(st.st_mode)) theme->roots.push_back(r);
    }
  }
  if (index_path.empty()) return theme;

  std::ifstream in(index_path);
  std::string line, section;
  std::vector<std::string> dir_names;
  std::map<std::string, Dir> dirs;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    if (line[0] == '[') {
      section = line.substr(1, line.find(']') - 1);
      continue;
    }
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
    if (section == "Icon Theme") {
      if (key == "Inherits") theme->inherits = split(val, ',');
      else if (key == "Directories" || key == "ScaledDirectories") {
        for (auto& d : split(val, ',')) dir_names.push_back(d);
      }
    } else {
      Dir& d = dirs[section];
      d.path = section;
      if (key == "Size") d.size = std::atoi(val.c_str());
      else if (key == "MinSize") d.min_size = std::atoi(val.c_str());
      else if (key == "MaxSize") d.max_size = std::atoi(val.c_str());
      else if (key == "Threshold") d.threshold = std::atoi(val.c_str());
      else if (key == "Type") d.type = val.empty() ? 'T' : val[0] == 'F' ? 'F' : val[0] == 'S' ? 'S' : 'T';
      else if (key == "Scale" && std::atoi(val.c_str()) != 1) d.size = -1;  // skip HiDPI duplicates
    }
  }
  for (const auto& dn : dir_names) {
    auto it = dirs.find(dn);
    if (it == dirs.end() || it->second.size < 0) continue;
    Dir d = it->second;
    if (d.min_size == 0) d.min_size = d.size;
    if (d.max_size == 0) d.max_size = d.size;
    theme->dirs.push_back(d);
  }
  return theme;
}

int distance(const Dir& d, int size) {
  switch (d.type) {
    case 'F': return std::abs(size - d.size);
    case 'S':
      if (size < d.min_size) return d.min_size - size;
      if (size > d.max_size) return size - d.max_size;
      return 0;
    default:
      if (size < d.size - d.threshold) return (d.size - d.threshold) - size;
      if (size > d.size + d.threshold) return size - (d.size + d.threshold);
      return 0;
  }
}

Theme& get_theme(const std::string& name) {
  static std::map<std::string, std::unique_ptr<Theme>> cache;
  auto it = cache.find(name);
  if (it == cache.end()) it = cache.emplace(name, parse_theme(name)).first;
  return *it->second;
}

std::string configured_theme() {
  const char* home = std::getenv("HOME");
  if (home) {
    for (const char* gtk : {"/.config/gtk-4.0/settings.ini", "/.config/gtk-3.0/settings.ini"}) {
      std::ifstream in(std::string(home) + gtk);
      std::string line;
      while (std::getline(in, line)) {
        line = trim(line);
        if (line.rfind("gtk-icon-theme-name", 0) == 0) {
          const size_t eq = line.find('=');
          if (eq != std::string::npos) {
            std::string v = trim(line.substr(eq + 1));
            if (!v.empty()) return v;
          }
        }
      }
    }
  }
  return "Adwaita";
}

// Theme search order: configured theme, its Inherits (breadth first), hicolor.
const std::vector<std::string>& theme_chain() {
  static std::vector<std::string> chain;
  if (!chain.empty()) return chain;
  std::set<std::string> seen;
  std::vector<std::string> queue{configured_theme()};
  for (size_t i = 0; i < queue.size(); ++i) {
    if (!seen.insert(queue[i]).second) continue;
    chain.push_back(queue[i]);
    for (const auto& p : get_theme(queue[i]).inherits) queue.push_back(p);
  }
  if (!seen.count("hicolor")) chain.push_back("hicolor");
  return chain;
}

std::string find_file_in_theme(Theme& t, const std::string& name, int size) {
  std::vector<std::pair<int, const Dir*>> order;
  for (const auto& d : t.dirs) order.push_back({distance(d, size), &d});
  std::stable_sort(order.begin(), order.end(),
                   [](const auto& a, const auto& b) { return a.first < b.first; });
  for (const auto& [dist, d] : order) {
    (void)dist;
    for (const auto& root : t.roots)
      for (const char* ext : {".png", ".svg"}) {
        const std::string p = root + "/" + t.name + "/" + d->path + "/" + name + ext;
        if (file_exists(p)) return p;
      }
  }
  return "";
}

std::string find_icon_file(const std::string& name, int size, const std::vector<std::string>& extra) {
  for (const auto& d : extra)
    for (const char* ext : {".png", ".svg"}) {
      const std::string p = d + "/" + name + ext;
      if (file_exists(p)) return p;
    }
  for (const auto& tn : theme_chain()) {
    std::string f = find_file_in_theme(get_theme(tn), name, size);
    if (!f.empty()) return f;
  }
  for (const char* ext : {".png", ".svg"}) {
    const std::string p = "/usr/share/pixmaps/" + name + ext;
    if (file_exists(p)) return p;
  }
  return "";
}

}  // namespace

cairo_surface_t* surface_from_rgba(const unsigned char* rgba, int w, int h) {
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) return nullptr;
  unsigned char* dst = cairo_image_surface_get_data(s);
  const int stride = cairo_image_surface_get_stride(s);
  cairo_surface_flush(s);
  for (int y = 0; y < h; ++y) {
    auto* row = reinterpret_cast<uint32_t*>(dst + static_cast<size_t>(y) * stride);
    for (int x = 0; x < w; ++x) {
      const unsigned char* p = rgba + (static_cast<size_t>(y) * w + x) * 4;
      const unsigned a = p[3];
      row[x] = (a << 24) | (((p[0] * a + 127) / 255) << 16) | (((p[1] * a + 127) / 255) << 8) |
               ((p[2] * a + 127) / 255);
    }
  }
  cairo_surface_mark_dirty(s);
  return s;
}

cairo_surface_t* load_icon(const std::string& name_in, int size, const std::vector<std::string>& extra) {
  if (name_in.empty()) return nullptr;
  std::string file;
  if (name_in[0] == '/') {
    if (file_exists(name_in)) file = name_in;
  } else {
    std::string name = name_in;
    while (true) {
      file = find_icon_file(name, size, extra);
      if (!file.empty()) break;
      const size_t dash = name.rfind('-');  // spec fallback: "a-b-c" -> "a-b" -> "a"
      if (dash == std::string::npos || dash == 0) break;
      name.resize(dash);
    }
  }
  if (file.empty()) return nullptr;
  const bool svg = file.size() > 4 && file.compare(file.size() - 4, 4, ".svg") == 0;
  Image img = svg ? load_svg(file, size) : load_image(file);
  if (!img.ok()) return nullptr;
  return surface_from_rgba(img.rgba.data(), img.width, img.height);
}

}  // namespace fleetwm::lean
