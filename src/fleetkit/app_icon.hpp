#pragma once

// The icon of a window, found from its app id through the desktop entries (id, Exec name, Name) and, failing that,
// through the app id used as an icon name. Shared by the taskbar and the window titlebars. Looked up once per app id
// for the life of the process; the first lookup reads the desktop entries.

#include <cairo.h>

#include <map>
#include <string>

namespace fleetwm::kit {

// The surface is owned by the cache (do not destroy it); nullptr when no icon was found. `size` is only used the first
// time an app id is asked for.
cairo_surface_t* app_icon(const std::string& app_id, int size = 48);

// Paints an icon returned by app_icon() at (x, y) in a size x size box of the current user space, with `alpha`. The icon is scaled to the
// device pixel size once per (icon, size) with the smooth filter and that copy is kept for the life of the process (app_icon surfaces are
// never freed, so the pointer is a safe key); drawing it again is a plain copy. Scaling the 48 px original with CAIRO_FILTER_BEST on
// every repaint took two thirds of the idle taskbar's CPU on the Celeron laptop. Only pass surfaces that stay alive for the rest of the process (app_icon() results, or icons a program loads once and never frees, like the launcher's entries): the copy is keyed by the surface address.
void draw_app_icon(cairo_t* cr, cairo_surface_t* icon, double x, double y, double size, double alpha = 1.0);

// Pure part, for the tests: desktop entry id / Exec name / Name (lower case) -> icon name.
std::string lower_ascii(std::string v);

}  // namespace fleetwm::kit
