#pragma once

// The icon of a window, found from its app id through the desktop entries (id, Exec name, Name) and, failing that,
// through the app id used as an icon name. Shared by the taskbar and the window titlebars. Looked up once per app id
// for the life of the process; the first lookup reads the desktop entries.

#include <cairo.h>

#include <map>
#include <string>
#include <vector>

namespace fleetwm::kit {

// The surface is owned by the cache (do not destroy it); nullptr when no icon was found. `size` is only used the first
// time an app id is asked for.
cairo_surface_t* app_icon(const std::string& app_id, int size = 48);

// Paints an icon returned by app_icon() at (x, y) in a size x size box of the current user space, with `alpha`. The icon is scaled to the
// device pixel size once per (icon, size) with the smooth filter and that copy is stored on the icon surface (cairo user data) and freed with it, so any image surface is safe to pass; drawing it again is a plain copy. Scaling the 48 px original with CAIRO_FILTER_BEST on
// every repaint took two thirds of the idle taskbar's CPU on the Celeron laptop. Only pass surfaces that stay alive for the rest of the process (app_icon() results, or icons a program loads once and never frees, like the launcher's entries): the copy is keyed by the surface address.
void draw_app_icon(cairo_t* cr, cairo_surface_t* icon, double x, double y, double size, double alpha = 1.0);

// The icon the compositor exported for a window: the picture a program set itself (_NET_WM_ICON of an X11 / Wine window, xdg-toplevel-icon of a Wayland
// one), kept as <cache>/fleetwm/window-icons/<view id>.png, or the icon name it gave (.name). nullptr when there is none yet. The surface belongs to the cache
// (app_icon's for a named icon, otherwise one owned here and kept until `forget_exported_window_icon`).
cairo_surface_t* exported_window_icon(unsigned view_id, int size = 48);
void forget_exported_window_icon(unsigned view_id);

// Pure parts, for the tests. lower_ascii: desktop entry id / Exec name / Name (lower case) -> icon name.
std::string lower_ascii(std::string v);
// The names an app id may be known by, most specific first: "Battle.net.exe" gives battle.net.exe, battle.net, battle-net, battlenet; "org.gnome.Calculator.desktop" gives
// org.gnome.calculator, calculator, gnome-calculator; a reverse-DNS id keeps its last words. Never empty entries, no duplicates.
std::vector<std::string> icon_name_candidates(const std::string& app_id);
// Icon theme names for the programs Wine ships (notepad.exe, winecfg.exe, regedit.exe, ...), best first; {} for any other name.
std::vector<std::string> wine_builtin_icon_names(const std::string& app_id);

}  // namespace fleetwm::kit
