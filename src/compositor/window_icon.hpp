#pragma once

#include <cstdint>

// The picture a program gives its window (_NET_WM_ICON, what Wine sets from a game's executable and many X11 programs set), written for the taskbar as
// $XDG_CACHE_HOME/fleetwm/window-icons/<window id>.png. The bar shows it instead of the letter on a disc a program without a desktop entry used to get.
// Needs the X11 window id; Wayland programs name their icon through the app id and a desktop entry, so they never come here.

namespace fleetwm {

// Reads the property from the X server `display_name` and writes the file; false when the window has no icon (yet), the property is empty or the server cannot be reached.
bool export_x11_window_icon(const char* display_name, uint32_t x11_window, uint32_t view_id);
void remove_window_icon(uint32_t view_id);
// A picture a Wayland program gave through xdg-toplevel-icon: premultiplied ARGB32 rows. False when the size is silly.
bool write_window_icon_argb(uint32_t view_id, int width, int height, int stride, const unsigned char* argb);
// An icon name instead of a picture: the bar looks it up in the icon theme.
void write_window_icon_name(uint32_t view_id, const char* name);
// A window id as the bar and the compositor both know it: the file path.
const char* window_icon_dir();

}  // namespace fleetwm
