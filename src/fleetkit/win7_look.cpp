#include "win7_look.hpp"

#include <algorithm>
#include <cmath>

namespace fleetwm::kit {

namespace {

void w7_rrect(cairo_t* cr, double x, double y, double w, double h, double r) {
  r = std::min(r, std::min(w, h) / 2);
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
  cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
  cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
  cairo_close_path(cr);
}

Color w7_mix(const Color& a, const Color& b, double t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0};
}

}  // namespace

void draw_taskbar_tile(cairo_t* cr, double x, double y, double w, double h, const TaskbarTile& st, double glow_x) {
  if (w < 6 || h < 6) return;
  const bool lit = st.running || st.hover || st.pressed;
  if (!lit) return;
  // Strength: a running tile is a light pane, the focused one brighter, hover brighter still, pressed darker.
  double top = st.running ? 0.34 : 0.22, mid = st.running ? 0.12 : 0.07;
  if (st.focused) top = 0.58, mid = 0.30;
  if (st.hover) top += 0.14, mid += 0.08;
  if (st.pressed) top *= 0.55, mid *= 0.5;
  cairo_save(cr);
  w7_rrect(cr, x + 0.5, y + 0.5, w - 1, h - 1, 3);
  cairo_clip(cr);
  cairo_pattern_t* g = cairo_pattern_create_linear(0, y, 0, y + h);
  cairo_pattern_add_color_stop_rgba(g, 0.00, 1, 1, 1, top);
  cairo_pattern_add_color_stop_rgba(g, 0.48, 1, 1, 1, mid);
  cairo_pattern_add_color_stop_rgba(g, 0.52, 1, 1, 1, mid * 0.35);  // the pane is lighter in its top half, with a soft step
  cairo_pattern_add_color_stop_rgba(g, 1.00, 1, 1, 1, st.focused ? 0.10 : 0.03);
  cairo_set_source(cr, g);
  cairo_paint(cr);
  cairo_pattern_destroy(g);
  if (st.hover && !st.pressed) {  // the light that follows the pointer along the tile
    const double gx = std::clamp(x + glow_x, x, x + w), gy = y + h * 0.9;
    cairo_pattern_t* r = cairo_pattern_create_radial(gx, gy, 0, gx, gy, std::max(20.0, w * 0.75));
    cairo_pattern_add_color_stop_rgba(r, 0, 1, 1, 1, 0.50);
    cairo_pattern_add_color_stop_rgba(r, 0.45, 1, 1, 1, 0.16);
    cairo_pattern_add_color_stop_rgba(r, 1, 1, 1, 1, 0.0);
    cairo_set_source(cr, r);
    cairo_paint(cr);
    cairo_pattern_destroy(r);
  }
  cairo_restore(cr);
  w7_rrect(cr, x + 0.5, y + 0.5, w - 1, h - 1, 3);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.55);
  cairo_set_line_width(cr, 1);
  cairo_stroke(cr);
  w7_rrect(cr, x + 1.5, y + 1.5, w - 3, h - 3, 2);
  cairo_set_source_rgba(cr, 1, 1, 1, st.focused ? 0.55 : 0.38);
  cairo_stroke(cr);
}

namespace {
void paint_orb(cairo_t* cr, double cx, double cy, double r, const Color& accent, bool hover, bool pressed) {
  cairo_save(cr);
  // shadow ring and dark rim
  cairo_arc(cr, cx, cy, r + 1.2, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.62);
  cairo_fill(cr);
  // body: light low, deep at the rim, in the detected colour (a third of the classic blue is kept so the orb still reads as glass)
  const Color blue_lo{0.55, 0.86, 1.0, 1}, blue_mid{0.16, 0.52, 0.86, 1}, blue_hi{0.02, 0.16, 0.42, 1};
  const Color white{1, 1, 1, 1}, black{0, 0, 0, 1};
  const Color lo = w7_mix(w7_mix(accent, white, 0.30), blue_lo, 0.25);
  const Color mid = w7_mix(w7_mix(accent, black, 0.12), blue_mid, 0.25);
  const Color hi = w7_mix(w7_mix(accent, black, 0.72), blue_hi, 0.25);
  cairo_pattern_t* body = cairo_pattern_create_radial(cx, cy + r * 0.55, r * 0.05, cx, cy + r * 0.1, r * 1.05);
  const double k = pressed ? 0.78 : (hover ? 1.18 : 1.0);
  auto stop = [&](double at, const Color& c) {
    cairo_pattern_add_color_stop_rgba(body, at, std::min(1.0, c.r * k), std::min(1.0, c.g * k), std::min(1.0, c.b * k), 1);
  };
  stop(0.0, lo);
  stop(0.45, mid);
  stop(1.0, hi);
  cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
  cairo_set_source(cr, body);
  cairo_fill(cr);
  cairo_pattern_destroy(body);
  // light inner rim
  cairo_arc(cr, cx, cy, r - 1.0, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.55);
  cairo_set_line_width(cr, 1);
  cairo_stroke(cr);
  // gloss over the top half, under the mark so the four panes stay crisp
  cairo_save(cr);
  cairo_arc(cr, cx, cy, r - 1.5, 0, 2 * M_PI);
  cairo_clip(cr);
  cairo_pattern_t* gloss = cairo_pattern_create_linear(0, cy - r, 0, cy + r * 0.15);
  cairo_pattern_add_color_stop_rgba(gloss, 0, 1, 1, 1, 0.42);
  cairo_pattern_add_color_stop_rgba(gloss, 1, 1, 1, 1, 0.03);
  cairo_set_source(cr, gloss);
  cairo_new_path(cr);
  cairo_save(cr);
  cairo_translate(cr, cx, cy - r * 0.42);
  cairo_scale(cr, 0.86, 0.50);
  cairo_arc(cr, 0, 0, r, 0, 2 * M_PI);
  cairo_restore(cr);
  cairo_fill(cr);
  cairo_pattern_destroy(gloss);
  cairo_restore(cr);
  // the mark: four panes, slightly tilted, larger than before and with a dark edge and shadow so they stand out from the orb in every colour
  cairo_save(cr);
  cairo_translate(cr, cx, cy - 0.5);
  cairo_rotate(cr, -0.14);
  const double sq = r * 0.46, gap = r * 0.07;
  const Color cols[4] = {w7_mix(accent, white, 0.78), w7_mix(accent, white, 0.42), w7_mix(accent, black, 0.12), w7_mix(accent, white, 0.60)};
  for (int i = 0; i < 4; ++i) {
    const double px = (i % 2 ? gap / 2 : -sq - gap / 2), py = (i / 2 ? gap / 2 : -sq - gap / 2);
    w7_rrect(cr, px + 0.5, py + 0.8, sq, sq, 1.5);  // shadow
    cairo_set_source_rgba(cr, 0, 0, 0, 0.45);
    cairo_fill(cr);
    cairo_pattern_t* pane = cairo_pattern_create_linear(0, py, 0, py + sq);
    cairo_pattern_add_color_stop_rgba(pane, 0, std::min(1.0, cols[i].r + 0.14), std::min(1.0, cols[i].g + 0.14), std::min(1.0, cols[i].b + 0.14), 1);
    cairo_pattern_add_color_stop_rgba(pane, 1, cols[i].r * 0.92, cols[i].g * 0.92, cols[i].b * 0.92, 1);
    w7_rrect(cr, px, py, sq, sq, 1.5);
    cairo_set_source(cr, pane);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.80);
    cairo_set_line_width(cr, 0.9);
    cairo_stroke(cr);
    cairo_pattern_destroy(pane);
  }
  cairo_restore(cr);
  cairo_restore(cr);
}

// The orb is five gradients, four panes and their shadows: about half a millisecond, painted on every repaint of the bar (every clock tick and every
// hover). One picture of it is kept and blitted; it is redrawn only when the size, colour, state or the output's scale changes.
struct OrbCache {
  cairo_surface_t* image = nullptr;
  double r = 0, sx = 0, sy = 0, cr_, cg, cb;
  bool hover = false, pressed = false;
  ~OrbCache() {
    if (image) cairo_surface_destroy(image);
  }
};
}  // namespace

void draw_start_orb(cairo_t* cr, double cx, double cy, double r, const Color& accent, bool hover, bool pressed) {
  static OrbCache cache;
  cairo_matrix_t m;
  cairo_get_matrix(cr, &m);
  const bool plain = std::fabs(m.xy) < 1e-9 && std::fabs(m.yx) < 1e-9 && m.xx > 0 && m.yy > 0;  // scaled only: the usual case, even with fractional scale
  if (!plain) return paint_orb(cr, cx, cy, r, accent, hover, pressed);
  const double half = r + 3;  // the shadow ring is r + 1.2 wide; a little room for the antialiasing
  if (!cache.image || cache.r != r || cache.sx != m.xx || cache.sy != m.yy || cache.cr_ != accent.r || cache.cg != accent.g || cache.cb != accent.b || cache.hover != hover ||
      cache.pressed != pressed) {
    if (cache.image) cairo_surface_destroy(cache.image);
    cache.image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, static_cast<int>(std::ceil(2 * half * m.xx)), static_cast<int>(std::ceil(2 * half * m.yy)));
    cairo_t* c = cairo_create(cache.image);
    cairo_scale(c, m.xx, m.yy);
    paint_orb(c, half, half, r, accent, hover, pressed);
    cairo_destroy(c);
    cache.r = r;
    cache.sx = m.xx;
    cache.sy = m.yy;
    cache.cr_ = accent.r;
    cache.cg = accent.g;
    cache.cb = accent.b;
    cache.hover = hover;
    cache.pressed = pressed;
  }
  // whole device pixels, so the blit is a copy and not a resample
  const double dx = std::round((cx - half) * m.xx + m.x0) , dy = std::round((cy - half) * m.yy + m.y0);
  cairo_save(cr);
  cairo_identity_matrix(cr);
  cairo_set_source_surface(cr, cache.image, dx, dy);
  cairo_paint(cr);
  cairo_restore(cr);
}

void draw_show_desktop_sliver(cairo_t* cr, double x, double y, double w, double h, bool hover) {
  cairo_save(cr);
  cairo_rectangle(cr, x, y, w, h);
  cairo_clip(cr);
  cairo_pattern_t* g = cairo_pattern_create_linear(x, 0, x + w, 0);
  cairo_pattern_add_color_stop_rgba(g, 0, 1, 1, 1, hover ? 0.42 : 0.10);
  cairo_pattern_add_color_stop_rgba(g, 1, 1, 1, 1, hover ? 0.28 : 0.04);
  cairo_set_source(cr, g);
  cairo_paint(cr);
  cairo_pattern_destroy(g);
  cairo_restore(cr);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.45);
  cairo_rectangle(cr, x, y, 1, h);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, 1, 1, 1, hover ? 0.55 : 0.30);
  cairo_rectangle(cr, x + 1, y, 1, h);
  cairo_fill(cr);
}

void paint_basic_taskbar(cairo_t* cr, double w, double h, const Color& tint, int top_edge) {
  const bool vertical = top_edge >= 2;
  const bool flip = top_edge == 1 || top_edge == 3;  // the desktop-facing edge is at the far end
  cairo_pattern_t* g = cairo_pattern_create_linear(0, 0, vertical ? w : 0, vertical ? 0 : h);
  const Color a = w7_mix(tint, Color{0, 0, 0, 1}, 0.40), b = w7_mix(tint, Color{0, 0, 0, 1}, 0.62);
  cairo_pattern_add_color_stop_rgba(g, flip ? 1.0 : 0.0, a.r, a.g, a.b, 1);
  cairo_pattern_add_color_stop_rgba(g, flip ? 0.0 : 1.0, b.r, b.g, b.b, 1);
  cairo_set_source(cr, g);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);
  cairo_pattern_destroy(g);
  auto line = [&](int offset, double r, double gg, double bb, double al) {
    cairo_set_source_rgba(cr, r, gg, bb, al);
    switch (top_edge) {
      case 0: cairo_rectangle(cr, 0, offset, w, 1); break;
      case 1: cairo_rectangle(cr, 0, h - 1 - offset, w, 1); break;
      case 2: cairo_rectangle(cr, offset, 0, 1, h); break;
      default: cairo_rectangle(cr, w - 1 - offset, 0, 1, h); break;
    }
    cairo_fill(cr);
  };
  line(0, 0, 0, 0, 0.60);   // dark outer edge
  line(1, 1, 1, 1, 0.38);   // light line under it
}

}  // namespace fleetwm::kit
