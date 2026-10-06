#pragma once

// The network icons, drawn with cairo (no image files): a Wi-Fi fan with the strength lit up,
// an Ethernet port, and signal bars. Shared by the bar and the network settings.

#include <cairo.h>

#include <cmath>

namespace fleetwm::net {

struct GlyphColor {
  double r, g, b, a;
};

inline void glyph_source(cairo_t* cr, const GlyphColor& c, double alpha_scale = 1.0) {
  cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a * alpha_scale);
}

// How many of the three Wi-Fi arcs are lit for a strength of 0..100.
inline int wifi_arcs_lit(int strength) { return strength >= 67 ? 3 : strength >= 34 ? 2 : strength > 0 ? 1 : 0; }
// How many of four signal bars are lit.
inline int signal_bars_lit(int strength) { return strength >= 75 ? 4 : strength >= 50 ? 3 : strength >= 25 ? 2 : strength > 0 ? 1 : 0; }

// A Wi-Fi fan centred on (cx, cy) in a box of `size`. `lit` arcs (0..3) are drawn solid, the
// rest faint. `crossed` adds a slash (radio off / no card).
inline void draw_wifi_glyph(cairo_t* cr, double cx, double cy, double size, int lit, const GlyphColor& color,
                            bool crossed = false) {
  cairo_save(cr);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_width(cr, std::max(1.3, size * 0.11));
  const double bx = cx, by = cy + size * 0.36;  // the dot at the bottom of the fan
  const double half = 0.82;                      // half-angle of the fan in radians
  for (int i = 0; i < 3; ++i) {
    const double radius = size * (0.30 + 0.24 * i);
    glyph_source(cr, color, i < lit ? 1.0 : 0.28);
    cairo_new_path(cr);
    cairo_arc(cr, bx, by, radius, -M_PI / 2 - half, -M_PI / 2 + half);
    cairo_stroke(cr);
  }
  glyph_source(cr, color, lit > 0 ? 1.0 : 0.28);
  cairo_arc(cr, bx, by, std::max(1.2, size * 0.075), 0, 2 * M_PI);
  cairo_fill(cr);
  if (crossed) {
    cairo_set_source_rgba(cr, 0.90, 0.25, 0.20, 1.0);
    cairo_set_line_width(cr, std::max(1.6, size * 0.12));
    cairo_move_to(cr, cx - size * 0.48, cy - size * 0.46);
    cairo_line_to(cr, cx + size * 0.48, cy + size * 0.5);
    cairo_stroke(cr);
  }
  cairo_restore(cr);
}

// An Ethernet (RJ45) port: a body with a notch at the bottom and four contacts. `lit` draws
// it solid (cable connected), otherwise faint.
inline void draw_ethernet_glyph(cairo_t* cr, double cx, double cy, double size, bool lit, const GlyphColor& color,
                                bool crossed = false) {
  cairo_save(cr);
  const double w = size * 0.92, h = size * 0.74;
  const double x = cx - w / 2, y = cy - h / 2;
  const double notch_w = w * 0.34, notch_h = h * 0.22;
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_set_line_width(cr, std::max(1.3, size * 0.10));
  glyph_source(cr, color, lit ? 1.0 : 0.35);
  cairo_new_path(cr);  // outline with the notch cut out of the bottom edge
  cairo_move_to(cr, x, y);
  cairo_line_to(cr, x + w, y);
  cairo_line_to(cr, x + w, y + h);
  cairo_line_to(cr, cx + notch_w / 2 + w * 0.12, y + h);
  cairo_line_to(cr, cx + notch_w / 2 + w * 0.12, y + h - notch_h);
  cairo_line_to(cr, cx - notch_w / 2 - w * 0.12, y + h - notch_h);
  cairo_line_to(cr, cx - notch_w / 2 - w * 0.12, y + h);
  cairo_line_to(cr, x, y + h);
  cairo_close_path(cr);
  cairo_stroke(cr);
  cairo_set_line_width(cr, std::max(1.2, size * 0.085));
  for (int i = 0; i < 4; ++i) {  // contacts
    const double px = x + w * (0.2 + 0.2 * i);
    cairo_move_to(cr, px, y + h * 0.16);
    cairo_line_to(cr, px, y + h * 0.5);
  }
  cairo_stroke(cr);
  if (crossed) {
    cairo_set_source_rgba(cr, 0.90, 0.25, 0.20, 1.0);
    cairo_set_line_width(cr, std::max(1.6, size * 0.12));
    cairo_move_to(cr, cx - size * 0.48, cy - size * 0.46);
    cairo_line_to(cr, cx + size * 0.48, cy + size * 0.5);
    cairo_stroke(cr);
  }
  cairo_restore(cr);
}

// Four rising bars, `lit` of them solid; (x, y) is the bottom-left corner.
inline void draw_signal_bars(cairo_t* cr, double x, double y, double h, int lit, const GlyphColor& color) {
  const double bw = h * 0.2, gap = h * 0.12;
  for (int i = 0; i < 4; ++i) {
    const double bh = h * (0.3 + 0.7 * i / 3.0);
    glyph_source(cr, color, i < lit ? 1.0 : 0.25);
    cairo_rectangle(cr, x + i * (bw + gap), y - bh, bw, bh);
    cairo_fill(cr);
  }
}

}  // namespace fleetwm::net
