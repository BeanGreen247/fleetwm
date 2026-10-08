#pragma once

// Draws a window titlebar (Desktop layout): the background (matte, or glass with a sheen), the title, and
// the Windows 7 style caption strip. The compositor renders it into a CPU buffer; it lives here, free of
// wlroots, so the unit tests and the benchmarks can render it too.

#include <cairo.h>

#include <string>

#include "fleetkit.hpp"
#include "window_geometry.hpp"

namespace fleetwm::kit {

struct TitlebarPaint {
  std::string title;
  bool focused = false;
  bool maximized = false;
  bool pinned = false;
  bool glass = false;  // translucent background with a sheen (the Windows Aero look); false: flat
  int hover_id = geom::kBtnNone;
  bool round_top = false;  // the outer top corners are rounded (not for a maximized or snapped window)
  int pressed_id = geom::kBtnNone;  // the caption button being held down
  geom::TitlebarLayout layout;  // where the caption buttons are and the span the title may use
  geom::TitleAlignment align = geom::TitleAlignment::Center;
};

// Paints the whole bar into `cr` (an ARGB surface of width x height, initially transparent). In the Desktop layout the
// bar is as wide as the window and carries no frame of its own: the frame (draw_frame_strip) starts under it.
void draw_titlebar(cairo_t* cr, int width, int height, const TitlebarPaint& paint, const Palette& palette);

// The glass background on its own (translucent colour, sheen, diagonal band, edge lines), kept as a finished
// picture per (size, focus, colour): painting it again is one blit. Exposed for the tests.
void paint_titlebar_glass_background(cairo_t* cr, int width, int height, bool focused, const Color& base);

// The left, right and bottom parts of a window frame, in the same glass (or flat) as the titlebar they hang
// from: translucent colour with a light line on the outer edge and a darker one against the content.
enum class FrameEdge { Left, Right, Bottom };
// `round_bottom` rounds the two outer bottom corners of the bottom strip (radius up to the strip's own height).
void draw_frame_strip(cairo_t* cr, int width, int height, FrameEdge edge, bool focused, bool glass, const Palette& palette,
                      bool round_bottom = false);

// The radius of the rounded outer corners of a window, in pixels.
inline constexpr double kWindowCornerRadius = 6.0;

// The window's outer edge as Windows 7 draws it: a dark 1 px line and, just inside it, a light 1 px line, both following the rounded
// corners. (x0,y0)-(x1,y1) is the whole window's outer rectangle in `cr`'s coordinates (it may reach beyond the surface when only one
// part of the frame is being drawn); r_* are the radii of its four corners, 0 for a square one. Painted over what is there.
void draw_window_edge(cairo_t* cr, double x0, double y0, double x1, double y1, double r_tl, double r_tr, double r_br, double r_bl,
                      bool glass, const Color& bg, const Palette& palette);

// How many glass backgrounds are cached and how many bytes they hold (tests, diagnostics).
struct TitlebarCacheStats {
  int entries = 0;
  size_t bytes = 0;
};
TitlebarCacheStats titlebar_glass_cache_stats();
void titlebar_glass_cache_clear();
// The same for the finished caption strips (one picture per button state).
TitlebarCacheStats titlebar_strip_cache_stats();
void titlebar_strip_cache_clear();

}  // namespace fleetwm::kit
