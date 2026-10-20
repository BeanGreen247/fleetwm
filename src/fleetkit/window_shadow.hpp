#pragma once

// The soft drop shadow of a window, as Windows 7 draws it. The compositor keeps one small picture per focus state and shows it around a
// window in nine pieces (four corners, four stretched edges), so a window of any size, and a window being dragged or resized, costs no
// new drawing. The picture is a window-sized-enough rounded rectangle blurred; only the part that lies outside the window is ever shown.

#include <cairo.h>

namespace fleetwm::kit {

inline constexpr int kShadowReach = 24;  // how far the shadow reaches beyond the window, in pixels

// The side of the square picture (reach on both sides of a body that is two reaches wide).
inline constexpr int kShadowSize = 4 * kShadowReach;

// Paints the picture into `cr` (an ARGB surface of kShadowSize x kShadowSize, initially transparent): the shadow of a rounded rectangle
// whose corners are at (reach, reach) and (size - reach, size - reach); the rectangle itself is left clear. A focused window's shadow is darker and wider than an unfocused one's.
void draw_window_shadow(cairo_t* cr, bool focused);

}  // namespace fleetwm::kit
