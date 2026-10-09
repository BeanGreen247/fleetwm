#pragma once

// The Bluetooth icon: the rune (a vertical line with two arrowheads) in the icon colour; a slash when Bluetooth is off; two dots
// either side of the rune when a device is connected, like the Windows 7 tray icon. Drawn with cairo, centred on (cx, cy), `size` tall.

#include <cairo.h>

#include <algorithm>

namespace fleetwm::kit {

enum class BluetoothState { Off, On, Connected };

inline void draw_bluetooth_glyph(cairo_t* cr, double cx, double cy, double size, BluetoothState state, double r, double g, double b) {
  cairo_save(cr);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  const double lw = std::max(1.2, size * 0.095);
  const double h = size, w = size * 0.56;  // the rune is 0.56 as wide as it is tall
  const double x0 = cx - w / 2, y0 = cy - h / 2;
  auto P = [&](double ux, double uy) { cairo_line_to(cr, x0 + ux * w, y0 + uy * h); };
  cairo_set_source_rgba(cr, r, g, b, state == BluetoothState::Off ? 0.5 : 1.0);
  cairo_set_line_width(cr, lw);
  cairo_new_path(cr);
  cairo_move_to(cr, x0 + 0.02 * w, y0 + 0.28 * h);  // upper left, down to the lower right
  P(0.98, 0.72);
  P(0.50, 1.00);  // bottom of the stem
  P(0.50, 0.00);  // up the stem
  P(0.98, 0.28);
  P(0.02, 0.72);  // down to the lower left
  cairo_stroke(cr);
  if (state == BluetoothState::Connected) {  // two dots, one each side
    const double rr = std::max(1.0, size * 0.075);
    cairo_arc(cr, x0 - size * 0.20, cy, rr, 0, 6.2832);
    cairo_fill(cr);
    cairo_arc(cr, x0 + w + size * 0.20, cy, rr, 0, 6.2832);
    cairo_fill(cr);
  }
  if (state == BluetoothState::Off) {
    cairo_set_source_rgba(cr, 0.90, 0.25, 0.20, 1.0);
    cairo_set_line_width(cr, std::max(1.5, size * 0.11));
    cairo_move_to(cr, cx - size * 0.46, cy - size * 0.46);
    cairo_line_to(cr, cx + size * 0.46, cy + size * 0.46);
    cairo_stroke(cr);
  }
  cairo_restore(cr);
}

}  // namespace fleetwm::kit
