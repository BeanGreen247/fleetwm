#pragma once

#include <string>

struct wlr_buffer;

#include "cursor_draw.hpp"

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

// The Windows 7 style pointer pictures (src/fleetkit/cursor_draw.hpp), rendered once per (shape, style, scale)
// into buffers that stay alive for the life of the compositor. Returns nullptr only if a buffer cannot be made.
wlr_buffer* cursor_picture(kit::CursorShape shape, bool glass, int scale);
// Drops every kept picture (the glass setting changed, or the compositor is shutting down).
void cursor_pictures_clear();
// Counters for the diagnostics and the tests: how many pictures are kept, how often one was reused.
kit::CursorCache<wlr_buffer*>& cursor_picture_cache();

}  // namespace fleetwm
