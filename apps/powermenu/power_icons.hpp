#pragma once

// The power menu's pictograms: one outline set on a 24 px cell, centred on (cx, cy), drawn with the
// current source colour. Every icon is a stroke of the same weight and fills roughly the same box, so
// they read as a family (the old set mixed a filled moon with thin outlines and different sizes).
// Header-only so a unit test can render each one and check where its ink lands.

#include <cairo.h>

#include <cmath>

#include "fleetkit.hpp"
#include "power_actions.hpp"

namespace fleetwm::power {

constexpr double kIconCell = 24;   // the box an icon is centred in
constexpr double kIconStroke = 2;  // line weight, px

inline void draw_icon(cairo_t* cr, int a, double cx, double cy) {
  cairo_save(cr);
  cairo_new_path(cr);
  cairo_set_line_width(cr, kIconStroke);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  switch (a) {
    case kLock: {  // padlock: body and a shackle that closes into it
      kit::rounded_rect(cr, cx - 7, cy - 1, 14, 10, 2.5);
      cairo_stroke(cr);
      cairo_move_to(cr, cx - 4.5, cy - 1);
      cairo_line_to(cr, cx - 4.5, cy - 4);
      cairo_arc(cr, cx, cy - 4, 4.5, M_PI, 2 * M_PI);
      cairo_line_to(cr, cx + 4.5, cy - 1);
      cairo_stroke(cr);
      cairo_arc(cr, cx, cy + 4, 1.1, 0, 2 * M_PI);  // keyhole
      cairo_fill(cr);
      break;
    }
    case kLogout:  // door frame with an arrow leaving it
      cairo_move_to(cr, cx - 1, cy - 8);
      cairo_line_to(cr, cx - 7.5, cy - 8);
      cairo_line_to(cr, cx - 7.5, cy + 8);
      cairo_line_to(cr, cx - 1, cy + 8);
      cairo_stroke(cr);
      cairo_move_to(cr, cx - 3.5, cy);
      cairo_line_to(cr, cx + 8, cy);
      cairo_move_to(cr, cx + 4, cy - 4);
      cairo_line_to(cr, cx + 8, cy);
      cairo_line_to(cr, cx + 4, cy + 4);
      cairo_stroke(cr);
      break;
    case kSleep: {  // crescent moon, outlined: the part of the big circle outside the offset one
      const double r1 = 8, r2 = 6.6, ox = 4.6, oy = -3.6;
      const double d = std::hypot(ox, oy), along = (r1 * r1 - r2 * r2 + d * d) / (2 * d);
      const double half = std::acos(along / r1), base = std::atan2(oy, ox);
      const double a1 = base + half, a2 = base - half;  // where the two circles cross, seen from the big one
      const double px1 = cx + r1 * std::cos(a1), py1 = cy + r1 * std::sin(a1);
      cairo_arc(cr, cx, cy, r1, a1, a2 + 2 * M_PI);       // the long way round, away from the small circle
      cairo_arc_negative(cr, cx + ox, cy + oy, r2,
                         std::atan2(cy + r1 * std::sin(a2) - (cy + oy), cx + r1 * std::cos(a2) - (cx + ox)),
                         std::atan2(py1 - (cy + oy), px1 - (cx + ox)));
      cairo_close_path(cr);
      cairo_stroke(cr);
      break;
    }
    case kReboot: {  // a circle that is almost closed, with an arrowhead at the open end
      const double r = 7.5, start = -M_PI / 2 + 0.75, end = -M_PI / 2 - 0.45 + 2 * M_PI;
      cairo_arc(cr, cx, cy, r, start, end);
      cairo_stroke(cr);
      const double ex = cx + r * std::cos(end), ey = cy + r * std::sin(end);
      const double tx = -std::sin(end), ty = std::cos(end), nx = std::cos(end), ny = std::sin(end);  // tangent (clockwise), normal
      cairo_move_to(cr, ex - 3 * tx + 3.6 * nx, ey - 3 * ty + 3.6 * ny);
      cairo_line_to(cr, ex + 1.2 * tx, ey + 1.2 * ty);
      cairo_line_to(cr, ex - 3 * tx - 3.6 * nx, ey - 3 * ty - 3.6 * ny);
      cairo_stroke(cr);
      break;
    }
    case kShutdown:  // the power symbol: a ring open at the top and a bar through the gap
      cairo_arc(cr, cx, cy + 0.5, 7.5, -M_PI / 2 + 0.65, -M_PI / 2 - 0.65 + 2 * M_PI);
      cairo_stroke(cr);
      cairo_move_to(cr, cx, cy - 9);
      cairo_line_to(cr, cx, cy - 0.5);
      cairo_stroke(cr);
      break;
  }
  cairo_restore(cr);
}

}  // namespace fleetwm::power
