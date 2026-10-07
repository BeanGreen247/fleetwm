#pragma once

// The volume icon: a speaker cone with up to three sound waves, or a red slash when no volume can be read.
// Drawn with cairo, centred on (cx, cy) in a box of `size`.

#include <cairo.h>

#include <algorithm>
#include <cmath>

namespace fleetwm::kit {

// Waves shown for a volume of 0..100: none at 0, one up to a third, two up to two thirds, then three.
inline int speaker_waves(int percent) { return percent <= 0 ? 0 : percent < 34 ? 1 : percent < 67 ? 2 : 3; }

inline void draw_speaker_glyph(cairo_t* cr, double cx, double cy, double size, int waves, double r, double g, double b,
                               bool crossed = false) {
  cairo_save(cr);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  const double lw = std::max(1.2, size * 0.09);
  const double x0 = cx - size * 0.46;  // left edge of the cone box
  cairo_set_source_rgba(cr, r, g, b, crossed ? 0.45 : 1.0);
  cairo_new_path(cr);  // box, then the flared cone
  cairo_move_to(cr, x0, cy - size * 0.16);
  cairo_line_to(cr, x0 + size * 0.20, cy - size * 0.16);
  cairo_line_to(cr, x0 + size * 0.46, cy - size * 0.38);
  cairo_line_to(cr, x0 + size * 0.46, cy + size * 0.38);
  cairo_line_to(cr, x0 + size * 0.20, cy + size * 0.16);
  cairo_line_to(cr, x0, cy + size * 0.16);
  cairo_close_path(cr);
  cairo_fill(cr);
  cairo_set_line_width(cr, lw);
  const double wx = x0 + size * 0.50;
  for (int i = 0; i < 3; ++i) {
    cairo_set_source_rgba(cr, r, g, b, i < waves ? 1.0 : 0.0);
    if (i >= waves) continue;
    const double radius = size * (0.18 + 0.15 * i);
    cairo_new_path(cr);
    cairo_arc(cr, wx, cy, radius, -0.75, 0.75);
    cairo_stroke(cr);
  }
  if (crossed) {
    cairo_set_source_rgba(cr, 0.90, 0.25, 0.20, 1.0);
    cairo_set_line_width(cr, std::max(1.5, size * 0.11));
    cairo_move_to(cr, cx - size * 0.46, cy - size * 0.46);
    cairo_line_to(cr, cx + size * 0.46, cy + size * 0.46);
    cairo_stroke(cr);
  }
  cairo_restore(cr);
}

}  // namespace fleetwm::kit
