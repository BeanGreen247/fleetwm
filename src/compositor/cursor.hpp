#pragma once

#include <string>

struct wlr_buffer;

namespace fleetwm {

// The xcursor theme to use: $XCURSOR_THEME if set, otherwise the first of a list of
// common ones that is installed with a "left_ptr" cursor (DMZ, Adwaita, Breeze, ...),
// otherwise "". A machine with no cursor theme at all used to end up with an
// invisible mouse pointer, in the compositor and in every app that loads its own.
std::string pick_cursor_theme();

// True if `theme` (a directory name under an icon path) has a left_ptr cursor.
bool cursor_theme_has_left_ptr(const std::string& theme);

// A built-in arrow pointer, drawn from a bitmap, for machines with no cursor theme.
// Returns a new buffer (caller drops its reference) and the hotspot.
wlr_buffer* create_fallback_cursor(int* hotspot_x, int* hotspot_y);

}  // namespace fleetwm
