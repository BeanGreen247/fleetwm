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

// Pure part, for the tests: desktop entry id / Exec name / Name (lower case) -> icon name.
std::string lower_ascii(std::string v);

}  // namespace fleetwm::kit
