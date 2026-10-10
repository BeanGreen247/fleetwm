#include "app_icon.hpp"

#include <algorithm>
#include <cctype>

#include "desktop_entry.hpp"
#include "icon_theme.hpp"

namespace fleetwm::kit {

std::string lower_ascii(std::string v) {
  std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return v;
}

cairo_surface_t* app_icon(const std::string& app_id, int size) {
  static std::map<std::string, cairo_surface_t*> icons;
  static std::map<std::string, std::string> names;  // lower-case app id / exec / name -> icon
  static bool names_loaded = false;
  const auto cached = icons.find(app_id);
  if (cached != icons.end()) return cached->second;
  if (!names_loaded) {
    names_loaded = true;
    for (const DesktopEntry& de : load_desktop_entries()) {
      if (de.icon.empty()) continue;
      std::string id = lower_ascii(de.id);
      if (id.size() > 8 && id.compare(id.size() - 8, 8, ".desktop") == 0) id.resize(id.size() - 8);
      names.emplace(id, de.icon);
      names.emplace(lower_ascii(exec_basename(de)), de.icon);
      names.emplace(lower_ascii(de.name), de.icon);
    }
  }
  cairo_surface_t* icon = nullptr;
  const auto named = names.find(lower_ascii(app_id));
  if (named != names.end()) icon = load_icon(named->second, size);
  if (!icon) {
    const std::string id = lower_ascii(app_id);
    if (id.find("taskmgr") != std::string::npos || id.find("taskmanager") != std::string::npos)
      icon = load_icon("fleetwm-taskmgr", size);
    else if (id.find("settings") != std::string::npos)
      icon = load_icon("fleetwm-settings", size);
    else if (id.find("filemanager") != std::string::npos || id.find("fleetwm-fm") != std::string::npos)
      icon = load_icon("fleetwm-fm", size);
  }
  if (!icon && !app_id.empty()) icon = load_icon(app_id, size);
  icons[app_id] = icon;
  return icon;
}

}  // namespace fleetwm::kit
