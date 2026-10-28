#include "app_icon.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

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


std::vector<std::string> icon_name_candidates(const std::string& app_id) {
  std::vector<std::string> out;
  auto add = [&](std::string v) {
    if (v.size() < 2) return;
    if (std::find(out.begin(), out.end(), v) == out.end()) out.push_back(std::move(v));
  };
  auto strip_suffix = [](std::string v, const char* suffix) {
    const std::string sfx = suffix;
    if (v.size() > sfx.size() && v.compare(v.size() - sfx.size(), sfx.size(), sfx) == 0) v.resize(v.size() - sfx.size());
    return v;
  };
  const std::string full = lower_ascii(app_id);
  add(full);
  std::string base = strip_suffix(strip_suffix(full, ".desktop"), ".exe");
  for (const char* sfx : {"-bin", "_bin", "-stable", "-launcher"}) base = strip_suffix(base, sfx);
  add(base);
  std::string slug;  // punctuation as hyphens: "battle.net" -> "battle-net", "my game" -> "my-game"
  std::string flat;  // and with none: "battlenet"
  for (char c : base) {
    const bool alnum = std::isalnum(static_cast<unsigned char>(c)) != 0;
    if (alnum) {
      slug += c;
      flat += c;
    } else if (!slug.empty() && slug.back() != '-') {
      slug += '-';
    }
  }
  while (!slug.empty() && slug.back() == '-') slug.pop_back();
  add(slug);
  add(flat);
  // reverse-DNS: org.gnome.Calculator -> calculator, gnome-calculator; com.valvesoftware.Steam -> steam
  std::vector<std::string> parts;
  for (size_t at = 0; at <= base.size();) {
    size_t dot = base.find('.', at);
    if (dot == std::string::npos) dot = base.size();
    if (dot > at) parts.push_back(base.substr(at, dot - at));
    at = dot + 1;
  }
  if (parts.size() >= 3) {
    add(parts.back());
    add(parts[parts.size() - 2] + "-" + parts.back());
  } else if (parts.size() == 2 && parts[0].size() <= 4) {
    add(parts.back());
  }
  return out;
}

std::vector<std::string> wine_builtin_icon_names(const std::string& app_id) {
  static const std::vector<std::pair<std::string, std::vector<std::string>>> table = {
      {"notepad", {"wine-notepad", "accessories-text-editor", "text-editor", "wine"}},
      {"wordpad", {"wine-wordpad", "accessories-text-editor", "libreoffice-writer", "wine"}},
      {"write", {"wine-wordpad", "accessories-text-editor", "wine"}},
      {"winecfg", {"wine-winecfg", "wine-configuration", "preferences-system", "wine"}},
      {"regedit", {"wine-regedit", "preferences-system", "wine"}},
      {"explorer", {"wine-explorer", "system-file-manager", "folder", "wine"}},
      {"winefile", {"wine-winefile", "system-file-manager", "folder", "wine"}},
      {"control", {"wine-control", "preferences-system", "wine"}},
      {"cmd", {"wine-cmd", "utilities-terminal", "terminal", "wine"}},
      {"wineconsole", {"wine-wineconsole", "utilities-terminal", "terminal", "wine"}},
      {"taskmgr", {"wine-taskmgr", "utilities-system-monitor", "wine"}},
      {"uninstaller", {"wine-uninstaller", "system-software-install", "wine"}},
      {"msiexec", {"wine-msiexec", "system-software-install", "wine"}},
      {"iexplore", {"wine-iexplore", "web-browser", "internet-web-browser", "wine"}},
      {"winebrowser", {"wine-winebrowser", "web-browser", "internet-web-browser", "wine"}},
      {"winemine", {"wine-winemine", "applications-games", "wine"}},
      {"clock", {"wine-clock", "clock", "wine"}},
      {"oleview", {"wine-oleview", "wine"}},
      {"eject", {"wine-eject", "media-eject", "wine"}},
  };
  std::string id = lower_ascii(app_id);
  const size_t slash = id.find_last_of("\\/");
  if (slash != std::string::npos) id = id.substr(slash + 1);
  if (id.size() > 4 && id.compare(id.size() - 4, 4, ".exe") == 0) id.resize(id.size() - 4);
  for (const auto& row : table)
    if (row.first == id) return row.second;
  return {};
}

// A Steam game's window has the app id steam_app_<id> and no desktop entry. Steam keeps the game's icon in its library cache (a small hash-named jpg beside the header and
// library pictures) and, for games with a shortcut, as steam_icon_<id> in the icon theme.
static cairo_surface_t* steam_game_icon(const std::string& app_id, int size) {
  if (app_id.rfind("steam_app_", 0) != 0) return nullptr;
  const std::string id = app_id.substr(10);
  if (id.empty() || id.find_first_not_of("0123456789") != std::string::npos || id == "0") return nullptr;
  if (cairo_surface_t* themed = load_icon("steam_icon_" + id, size)) return themed;
  const char* home = std::getenv("HOME");
  if (!home) return nullptr;
  const std::string dir = std::string(home) + "/.local/share/Steam/appcache/librarycache/" + id;
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
    const std::string name = e.path().filename().string();
    if (name.size() == 44 && name.compare(40, 4, ".jpg") == 0)  // <40 hex digits>.jpg
      if (cairo_surface_t* icon = load_icon(e.path().string(), size)) return icon;
  }
  return nullptr;
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
      if (!de.startup_wm_class.empty()) names.emplace(lower_ascii(de.startup_wm_class), de.icon);
      // a Wine / Proton shortcut starts "env WINEPREFIX=... wine start /unix .../Game.exe": the program's file name is how its window is known (Game.exe)
      for (const std::string& arg : exec_argv(de)) {
        const std::string a = lower_ascii(arg);
        if (a.size() > 4 && a.compare(a.size() - 4, 4, ".exe") == 0) {
          const size_t slash = a.find_last_of("\\/");
          names.emplace(slash == std::string::npos ? a : a.substr(slash + 1), de.icon);
        }
      }
      for (const std::string& c : icon_name_candidates(id)) names.emplace(c, de.icon);
    }
  }
  cairo_surface_t* icon = nullptr;
  const std::vector<std::string> candidates = icon_name_candidates(app_id);
  for (const std::string& c : candidates) {
    const auto named = names.find(c);
    if (named != names.end() && (icon = load_icon(named->second, size))) break;
  }
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
  for (size_t i = 1; !icon && i < candidates.size(); ++i) icon = load_icon(candidates[i], size);  // the app id as an icon name, in its other spellings
  if (!icon) icon = steam_game_icon(app_id, size);
  if (!icon) {  // Wine's own programs (notepad.exe, winecfg.exe ...), then any Windows program without an icon of its own
    for (const std::string& n : wine_builtin_icon_names(app_id))
      if ((icon = load_icon(n, size))) break;
    const std::string low = lower_ascii(app_id);
    if (!icon && low.size() > 4 && low.compare(low.size() - 4, 4, ".exe") == 0) {
      for (const char* n : {"wine", "application-x-wine-extension-exe", "application-x-ms-dos-executable", "application-x-executable"})
        if ((icon = load_icon(n, size))) break;
    }
  }
  icons[app_id] = icon;
  return icon;
}

namespace {
std::map<unsigned, cairo_surface_t*>& exported_icons() {
  static std::map<unsigned, cairo_surface_t*> m;
  return m;
}
}  // namespace

cairo_surface_t* exported_window_icon(unsigned view_id, int size) {
  auto& cache = exported_icons();
  const auto hit = cache.find(view_id);
  if (hit != cache.end()) return hit->second;
  const char* xdg = std::getenv("XDG_CACHE_HOME");
  const char* home = std::getenv("HOME");
  const std::string base = xdg && *xdg ? std::string(xdg) : (home ? std::string(home) + "/.cache" : std::string());
  if (base.empty()) return nullptr;
  const std::string stem = base + "/fleetwm/window-icons/" + std::to_string(view_id);
  cairo_surface_t* png = cairo_image_surface_create_from_png((stem + ".png").c_str());
  if (cairo_surface_status(png) == CAIRO_STATUS_SUCCESS) {
    cache[view_id] = png;
    return png;
  }
  cairo_surface_destroy(png);
  std::ifstream named(stem + ".name");
  std::string icon_name;
  if (named && std::getline(named, icon_name) && !icon_name.empty())
    if (cairo_surface_t* themed = app_icon(icon_name, size)) return themed;  // owned by app_icon's cache; not remembered here, a later PNG may replace it
  return nullptr;
}

void forget_exported_window_icon(unsigned view_id) {
  auto& cache = exported_icons();
  const auto it = cache.find(view_id);
  if (it == cache.end()) return;
  cairo_surface_destroy(it->second);
  cache.erase(it);
}

namespace {
// The scaled copies hang off the icon surface itself (cairo user data), so they are freed with it and
// a new surface that happens to get a freed one's address never sees a stale copy.
struct ScaledCopies {
  std::vector<std::pair<int, cairo_surface_t*>> by_px;
  ~ScaledCopies() {
    for (auto& e : by_px) cairo_surface_destroy(e.second);
  }
};
const cairo_user_data_key_t kScaledKey = {};
}  // namespace

void draw_app_icon(cairo_t* cr, cairo_surface_t* icon, double x, double y, double size, double alpha) {
  const int iw = cairo_image_surface_get_width(icon), ih = cairo_image_surface_get_height(icon);
  if (iw <= 0 || ih <= 0 || size <= 0) return;
  double ux = size, uy = 0;
  cairo_user_to_device_distance(cr, &ux, &uy);  // the device pixels the box covers (the surface scale of a HiDPI output is in the matrix)
  const int px = std::max(1, static_cast<int>(std::lround(std::hypot(ux, uy))));
  auto* copies = static_cast<ScaledCopies*>(cairo_surface_get_user_data(icon, &kScaledKey));
  if (!copies) {
    copies = new ScaledCopies;
    cairo_surface_set_user_data(icon, &kScaledKey, copies, [](void* d) { delete static_cast<ScaledCopies*>(d); });
  }
  cairo_surface_t* scaled = nullptr;
  for (const auto& e : copies->by_px)
    if (e.first == px) scaled = e.second;
  if (!scaled) {
    scaled = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
    cairo_t* c = cairo_create(scaled);
    cairo_scale(c, static_cast<double>(px) / iw, static_cast<double>(px) / ih);
    cairo_set_source_surface(c, icon, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(c), CAIRO_FILTER_BEST);
    cairo_paint(c);
    cairo_destroy(c);
    copies->by_px.emplace_back(px, scaled);
  }
  cairo_save(cr);
  cairo_translate(cr, x, y);
  cairo_scale(cr, size / px, size / px);
  cairo_set_source_surface(cr, scaled, 0, 0);
  cairo_paint_with_alpha(cr, alpha);
  cairo_restore(cr);
}

}  // namespace fleetwm::kit
