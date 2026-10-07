#pragma once

// The network icons, drawn with cairo (no image files) in the Windows 7 style: Wi-Fi and mobile
// data as rising signal bars, wired as a computer with a cable, and signal bars for the lists. Shared by the bar and the network settings.

#include <cairo.h>

#include <algorithm>
#include <cmath>

namespace fleetwm::net {

struct GlyphColor {
  double r, g, b, a;
};

inline void glyph_source(cairo_t* cr, const GlyphColor& c, double alpha_scale = 1.0) {
  cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a * alpha_scale);
}

// How many of the five Wi-Fi / mobile bars are lit for a strength of 0..100.
inline int wifi_bars_lit(int strength) {
  return strength >= 80 ? 5 : strength >= 60 ? 4 : strength >= 40 ? 3 : strength >= 20 ? 2 : strength > 0 ? 1 : 0;
}
// How many of four signal bars are lit.
inline int signal_bars_lit(int strength) { return strength >= 75 ? 4 : strength >= 50 ? 3 : strength >= 25 ? 2 : strength > 0 ? 1 : 0; }

// The Windows 7 look: lit bars are green, the rest and the outlines follow the theme colour.
inline constexpr GlyphColor kWin7Green{0.36, 0.80, 0.30, 1.0};
inline constexpr GlyphColor kWin7Screen{0.40, 0.66, 0.96, 1.0};

inline void glyph_slash(cairo_t* cr, double cx, double cy, double size) {
  cairo_set_source_rgba(cr, 0.90, 0.25, 0.20, 1.0);
  cairo_set_line_width(cr, std::max(1.6, size * 0.12));
  cairo_move_to(cr, cx - size * 0.48, cy - size * 0.46);
  cairo_line_to(cr, cx + size * 0.48, cy + size * 0.5);
  cairo_stroke(cr);
}

// Wi-Fi as Windows 7 draws it: five rising bars with slanted tops, `lit` (0..5) of them green and
// the rest faint. Centred on (cx, cy) in a box of `size`. `crossed` adds a red slash.
inline void draw_wifi_glyph(cairo_t* cr, double cx, double cy, double size, int lit, const GlyphColor& color,
                            bool crossed = false) {
  cairo_save(cr);
  const double bw = size * 0.13, gap = size * 0.065, total = 5 * bw + 4 * gap;
  const double x0 = cx - total / 2, base = cy + size * 0.42, slant = size * 0.09;
  for (int i = 0; i < 5; ++i) {
    const double bh = size * (0.22 + 0.17 * i);
    const double x = x0 + i * (bw + gap);
    if (i < lit) glyph_source(cr, kWin7Green);
    else glyph_source(cr, color, 0.28);
    cairo_new_path(cr);
    cairo_move_to(cr, x, base);
    cairo_line_to(cr, x, base - bh + slant);
    cairo_line_to(cr, x + bw, base - bh);
    cairo_line_to(cr, x + bw, base);
    cairo_close_path(cr);
    cairo_fill(cr);
  }
  if (crossed) glyph_slash(cr, cx, cy, size);
  cairo_restore(cr);
}

// Mobile data: five straight rising bars on the right and an antenna mast on the left, so it
// cannot be mistaken for Wi-Fi.
inline void draw_mobile_glyph(cairo_t* cr, double cx, double cy, double size, int lit, const GlyphColor& color,
                              bool crossed = false) {
  cairo_save(cr);
  const double bw = size * 0.10, gap = size * 0.06, mast = size * 0.16;
  const double total = mast + 5 * bw + 4 * gap + size * 0.06;
  const double x0 = cx - total / 2, base = cy + size * 0.42;
  glyph_source(cr, color, lit > 0 ? 1.0 : 0.35);  // mast: a thin pole with a dot on top
  cairo_set_line_width(cr, std::max(1.2, size * 0.09));
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, x0 + mast * 0.4, base);
  cairo_line_to(cr, x0 + mast * 0.4, base - size * 0.62);
  cairo_stroke(cr);
  cairo_arc(cr, x0 + mast * 0.4, base - size * 0.68, std::max(1.3, size * 0.07), 0, 2 * M_PI);
  cairo_fill(cr);
  for (int i = 0; i < 5; ++i) {
    const double bh = size * (0.20 + 0.16 * i);
    const double x = x0 + mast + size * 0.06 + i * (bw + gap);
    if (i < lit) glyph_source(cr, kWin7Green);
    else glyph_source(cr, color, 0.28);
    cairo_rectangle(cr, x, base - bh, bw, bh);
    cairo_fill(cr);
  }
  if (crossed) glyph_slash(cr, cx, cy, size);
  cairo_restore(cr);
}

// Wired network as Windows 7 draws it: a small computer (blue glass screen, stand) with the
// cable running down from it. `lit` = cable connected; otherwise grey with no cable.
inline void draw_ethernet_glyph(cairo_t* cr, double cx, double cy, double size, bool lit, const GlyphColor& color,
                                bool crossed = false) {
  cairo_save(cr);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  const double w = size * 0.80, h = size * 0.52;
  const double x = cx - w / 2, y = cy - size * 0.44;
  const double lw = std::max(1.2, size * 0.085);
  cairo_set_line_width(cr, lw);
  cairo_new_path(cr);  // screen: outline, then the glass inside it
  cairo_rectangle(cr, x, y, w, h);
  glyph_source(cr, color, lit ? 1.0 : 0.45);
  cairo_stroke(cr);
  cairo_rectangle(cr, x + lw, y + lw, w - 2 * lw, h - 2 * lw);
  if (lit) glyph_source(cr, kWin7Screen, 0.9);
  else glyph_source(cr, color, 0.18);
  cairo_fill(cr);
  const double neck = y + h;  // stand and base
  glyph_source(cr, color, lit ? 1.0 : 0.45);
  cairo_move_to(cr, cx, neck);
  cairo_line_to(cr, cx, neck + size * 0.10);
  cairo_move_to(cr, cx - size * 0.18, neck + size * 0.10);
  cairo_line_to(cr, cx + size * 0.18, neck + size * 0.10);
  cairo_stroke(cr);
  if (lit) {  // the cable: down from the base and out to the right
    cairo_move_to(cr, cx, neck + size * 0.10);
    cairo_line_to(cr, cx, neck + size * 0.26);
    cairo_line_to(cr, cx + size * 0.30, neck + size * 0.26);
    cairo_stroke(cr);
  }
  if (crossed) glyph_slash(cr, cx, cy, size);
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
