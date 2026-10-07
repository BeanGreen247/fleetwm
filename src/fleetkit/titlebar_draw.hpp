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
void draw_frame_strip(cairo_t* cr, int width, int height, FrameEdge edge, bool focused, bool glass, const Palette& palette);

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
