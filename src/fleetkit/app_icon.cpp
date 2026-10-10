#include "app_icon.hpp"

#include <algorithm>
#include <cctype>

#include "desktop_entry.hpp"
#include "icon_theme.hpp"

namespace fleetwm::kit {

namespace {

cairo_surface_t* generated_fleetwm_icon(const std::string& id, int size) {
  enum class Kind { None, FileManager, Settings, TaskManager } kind = Kind::None;
  if (id.find("filemanager") != std::string::npos || id.find("fleetwm-fm") != std::string::npos) kind = Kind::FileManager;
  else if (id.find("settings") != std::string::npos) kind = Kind::Settings;
  else if (id.find("taskmgr") != std::string::npos || id.find("taskmanager") != std::string::npos) kind = Kind::TaskManager;
  if (kind == Kind::None) return nullptr;

  cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
  cairo_t* cr = cairo_create(surface);
  cairo_scale(cr, size, size);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  if (kind == Kind::FileManager) {
    cairo_move_to(cr, .10, .78); cairo_line_to(cr, .10, .22); cairo_curve_to(cr, .10, .17, .13, .14, .18, .14);
    cairo_line_to(cr, .40, .14); cairo_line_to(cr, .48, .23); cairo_line_to(cr, .86, .23);
    cairo_curve_to(cr, .91, .23, .94, .26, .94, .31); cairo_line_to(cr, .94, .78); cairo_close_path(cr);
    cairo_set_source_rgb(cr, .12, .35, .67); cairo_fill(cr);
    cairo_rectangle(cr, .10, .34, .84, .48); cairo_set_source_rgb(cr, .28, .62, .88); cairo_fill(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    for (int row = 0; row < 2; ++row)
      for (int col = 0; col < 2; ++col) cairo_rectangle(cr, .23 + col * .17, .45 + row * .13, .13, .09);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, .05, .16, .32); cairo_set_line_width(cr, .025); cairo_move_to(cr, .12, .35); cairo_line_to(cr, .91, .35); cairo_stroke(cr);
  } else if (kind == Kind::TaskManager) {
    cairo_set_source_rgb(cr, .13, .42, .72); cairo_rectangle(cr, .12, .15, .76, .70); cairo_fill(cr);
    cairo_set_source_rgb(cr, 1, 1, 1); cairo_rectangle(cr, .20, .25, .60, .50); cairo_fill(cr);
    cairo_set_source_rgb(cr, .20, .58, .37); cairo_rectangle(cr, .27, .60, .08, .09); cairo_rectangle(cr, .40, .48, .08, .21); cairo_rectangle(cr, .53, .36, .08, .33); cairo_rectangle(cr, .66, .53, .08, .16); cairo_fill(cr);
  } else {
    cairo_set_source_rgb(cr, .33, .45, .62); cairo_rectangle(cr, .14, .14, .72, .72); cairo_fill(cr);
    cairo_set_source_rgb(cr, 1, 1, 1); cairo_set_line_width(cr, .07);
    for (double y : {.31, .50, .69}) { cairo_move_to(cr, .27, y); cairo_line_to(cr, .73, y); }
    cairo_stroke(cr);
    cairo_set_source_rgb(cr, .22, .62, .90);
    for (double x : {.40, .61, .49}) { cairo_arc(cr, x, x == .49 ? .69 : (x == .40 ? .31 : .50), .065, 0, 2 * 3.141592653589793); cairo_fill(cr); }
  }
  cairo_destroy(cr);
  return surface;
}

}  // namespace

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
  if (cairo_surface_t* generated = generated_fleetwm_icon(lower_ascii(app_id), size)) {
    icons[app_id] = generated;
    return generated;
  }
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
