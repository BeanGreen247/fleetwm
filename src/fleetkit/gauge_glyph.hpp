#pragma once

// The power-mode icon: a tachometer. A 240 degree dial that runs from lower left over the top to
// lower right, a red zone over its last fifth, and a needle at `fraction` (0 = idle, 1 = pegged on
// the edge of the red zone). Drawn with cairo, centred on (cx, cy) in a box of `size`.

#include <cairo.h>

#include <algorithm>
#include <cmath>

namespace fleetwm::kit {

inline constexpr double kGaugeStartDeg = 150.0, kGaugeSweepDeg = 240.0, kGaugeRedFrom = 0.8;

// Needle angle in radians (cairo: clockwise from +x, y down) for a fraction of the dial.
inline double gauge_angle(double fraction) {
  const double f = std::clamp(fraction, 0.0, 1.0);
  return (kGaugeStartDeg + kGaugeSweepDeg * f) * M_PI / 180.0;
}

inline void draw_gauge_glyph(cairo_t* cr, double cx, double cy, double size, double fraction, double r, double g, double b) {
  cairo_save(cr);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  const double radius = size * 0.46, lw = std::max(1.3, size * 0.10);
  const double hub_y = cy + size * 0.06;
  cairo_set_line_width(cr, lw);
  cairo_new_path(cr);
  cairo_set_source_rgba(cr, r, g, b, 1.0);
  cairo_arc(cr, cx, hub_y, radius, gauge_angle(0.0), gauge_angle(kGaugeRedFrom));
  cairo_stroke(cr);
  cairo_new_path(cr);  // the red zone
  cairo_set_source_rgba(cr, 0.93, 0.24, 0.20, 1.0);
  cairo_arc(cr, cx, hub_y, radius, gauge_angle(kGaugeRedFrom), gauge_angle(1.0));
  cairo_stroke(cr);
  const double a = gauge_angle(fraction);  // needle, red once it is in the red zone
  if (fraction >= kGaugeRedFrom) cairo_set_source_rgba(cr, 0.93, 0.24, 0.20, 1.0);
  else cairo_set_source_rgba(cr, r, g, b, 1.0);
  cairo_set_line_width(cr, std::max(1.2, size * 0.09));
  cairo_new_path(cr);
  cairo_move_to(cr, cx, hub_y);
  cairo_line_to(cr, cx + std::cos(a) * radius * 0.78, hub_y + std::sin(a) * radius * 0.78);
  cairo_stroke(cr);
  cairo_arc(cr, cx, hub_y, std::max(1.2, size * 0.075), 0, 2 * M_PI);
  cairo_fill(cr);
  cairo_restore(cr);
}

}  // namespace fleetwm::kit
