#include "caption_buttons.hpp"

#include <algorithm>
#include <cmath>

namespace fleetwm::kit {

namespace {

constexpr int kMaximize = 0, kClose = 1, kMinimize = 2, kPin = 3;  // window_geometry.hpp's TitleButton ids

struct Rgb {
  double r, g, b;
};

Rgb lerp(const Rgb& a, const Rgb& b, double t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
Rgb rgb_of(const Color& c) { return {c.r, c.g, c.b}; }
double luma(const Rgb& c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }
constexpr Rgb kWhite = {1, 1, 1}, kBlack = {0, 0, 0};

// A vertical glass gradient: lighter upper half, a crisp split in the middle, darker lower half that
// lightens again at the very bottom.
struct Glass {
  Rgb top, upper_end, lower_start, lower, bottom;
};

// Builds the glass of one button from a base colour. On a dark base the shine is a gentle lift and the
// lower half sinks a little; on a light base the shine is bright and the lower half is clearly darker.
Glass glass_from(const Rgb& base, bool dark, double shine = 1.0) {
  Glass g;
  g.top = lerp(base, kWhite, (dark ? 0.26 : 0.58) * shine);
  g.upper_end = lerp(base, kWhite, (dark ? 0.12 : 0.30) * shine);
  g.lower_start = lerp(base, kBlack, dark ? 0.10 : 0.15);
  g.lower = lerp(base, kBlack, dark ? 0.18 : 0.21);
  g.bottom = lerp(base, kWhite, dark ? 0.05 : 0.12);
  return g;
}

constexpr Rgb kRed = {0.80, 0.26, 0.20}, kRedLit = {0.98, 0.46, 0.15}, kGlowRed = {1.00, 0.60, 0.12};

// The glass of a button in its current state, from the theme colours.
Glass glass_for(int id, bool hover, bool pressed, bool toggled, const CaptionState& st, bool dark) {
  const Rgb bg = rgb_of(st.colors.bg), fg = rgb_of(st.colors.fg), accent = rgb_of(st.colors.accent);
  if (id == kClose) {
    const Rgb base = lerp(kRed, bg, dark ? 0.24 : 0.0);  // a deep red on a dark bar, not a flare
    if (pressed) return glass_from(lerp(kRed, kBlack, 0.45), true);
    if (hover) return glass_from(kRedLit, false, 0.9);
    return glass_from(base, dark, 1.1);
  }
  if (pressed) return glass_from(lerp(accent, kBlack, 0.45), true);
  if (hover) return glass_from(lerp(accent, bg, 0.12), false, 0.85);
  if (toggled) return glass_from(lerp(accent, kBlack, 0.40), true);  // a pinned window's pin looks pushed in
  // Neutral: the titlebar's own colour, raised a little towards the text colour and tinted by the accent.
  const Rgb base = lerp(lerp(bg, fg, dark ? 0.15 : 0.07), accent, 0.10);
  return glass_from(base, dark);
}

void rgba(cairo_t* cr, const Rgb& c, double a) { cairo_set_source_rgba(cr, c.r, c.g, c.b, a); }

// Rectangle whose top corners are square and whose bottom corners have the given radii.
void strip_path(cairo_t* cr, double x, double y, double w, double h, double r_left, double r_right) {
  cairo_new_sub_path(cr);
  cairo_move_to(cr, x, y);
  cairo_line_to(cr, x + w, y);
  cairo_line_to(cr, x + w, y + h - r_right);
  if (r_right > 0) cairo_arc(cr, x + w - r_right, y + h - r_right, r_right, 0, M_PI / 2);
  cairo_line_to(cr, x + r_left, y + h);
  if (r_left > 0) cairo_arc(cr, x + r_left, y + h - r_left, r_left, M_PI / 2, M_PI);
  cairo_close_path(cr);
}

void fill_glass(cairo_t* cr, const Glass& g, double x, double y, double w, double h, double alpha) {
  cairo_pattern_t* p = cairo_pattern_create_linear(0, y, 0, y + h);
  cairo_pattern_add_color_stop_rgba(p, 0.00, g.top.r, g.top.g, g.top.b, alpha);
  cairo_pattern_add_color_stop_rgba(p, 0.47, g.upper_end.r, g.upper_end.g, g.upper_end.b, alpha);
  cairo_pattern_add_color_stop_rgba(p, 0.53, g.lower_start.r, g.lower_start.g, g.lower_start.b, alpha);
  cairo_pattern_add_color_stop_rgba(p, 0.86, g.lower.r, g.lower.g, g.lower.b, alpha);
  cairo_pattern_add_color_stop_rgba(p, 1.00, g.bottom.r, g.bottom.g, g.bottom.b, alpha);
  cairo_set_source(cr, p);
  cairo_rectangle(cr, x, y, w, h);
  cairo_fill(cr);
  cairo_pattern_destroy(p);
}


// What a glyph is drawn with: its colour, the outline round it, and the colour of the "hole" of a window glyph
// (the glass behind it).
struct Ink {
  Rgb fill, outline, hole;
};

// A glyph is drawn twice: a wide stroke in the outline colour, then the shape on top in the glyph colour,
// which leaves an outline that keeps it readable on any glass.
void outlined(cairo_t* cr, bool fill_shape, double width, double alpha, const Ink& ink) {
  cairo_save(cr);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  rgba(cr, ink.outline, 0.85 * alpha);
  cairo_set_line_width(cr, width + 2.2);
  cairo_stroke_preserve(cr);
  rgba(cr, ink.fill, alpha);
  if (fill_shape) cairo_fill_preserve(cr);
  cairo_set_line_width(cr, width);
  if (!fill_shape) cairo_stroke(cr);
  else cairo_new_path(cr);
  cairo_restore(cr);
}

void rounded_box(cairo_t* cr, double x, double y, double w, double h, double r) {
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
  cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
  cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
  cairo_close_path(cr);
}

void glyph_close(cairo_t* cr, double cx, double cy, double s, double alpha, const Ink& ink) {
  const double r = s * 0.42;
  cairo_new_path(cr);
  cairo_move_to(cr, cx - r, cy - r);
  cairo_line_to(cr, cx + r, cy + r);
  cairo_move_to(cr, cx + r, cy - r);
  cairo_line_to(cr, cx - r, cy + r);
  outlined(cr, false, std::max(2.0, s * 0.26), alpha, ink);
}

void glyph_minimize(cairo_t* cr, double cx, double cy, double s, double alpha, const Ink& ink) {
  const double w = s * 0.82, h = std::max(2.4, s * 0.24);
  cairo_new_path(cr);
  rounded_box(cr, cx - w / 2, cy + s * 0.16, w, h, h / 2);
  outlined(cr, true, 0.6, alpha, ink);
}

// A window: frame with a small hole.
void glyph_maximize(cairo_t* cr, double cx, double cy, double s, double alpha, const Ink& ink) {
  const double w = s * 0.78, h = s * 0.66, x = cx - w / 2, y = cy - h / 2 - s * 0.02;
  cairo_new_path(cr);
  rounded_box(cr, x, y, w, h, 1.0);
  outlined(cr, true, 0.6, alpha, ink);
  // the hole, in the glass colour behind the glyph
  const double t = std::max(1.6, s * 0.2);
  cairo_new_path(cr);
  cairo_rectangle(cr, x + t, y + t + 0.6, w - 2 * t, h - 2 * t - 0.6);
  rgba(cr, ink.hole, 0.8 * alpha);
  cairo_fill(cr);
}

// Restore: two windows, one behind the other.
void glyph_restore(cairo_t* cr, double cx, double cy, double s, double alpha, const Ink& ink) {
  const double w = s * 0.6, h = s * 0.5;
  const double bx = cx - w / 2 + s * 0.14, by = cy - h / 2 - s * 0.16;  // back window, up and right
  cairo_new_path(cr);
  rounded_box(cr, bx, by, w, h, 0.8);
  outlined(cr, true, 0.5, alpha * 0.95, ink);
  const double fx = cx - w / 2 - s * 0.14, fy = cy - h / 2 + s * 0.2;  // front window, down and left
  cairo_new_path(cr);
  rounded_box(cr, fx, fy, w, h, 0.8);
  outlined(cr, true, 0.5, alpha, ink);
  const double t = std::max(1.4, s * 0.17);
  cairo_new_path(cr);
  cairo_rectangle(cr, fx + t, fy + t + 0.5, w - 2 * t, h - 2 * t - 0.5);
  rgba(cr, ink.hole, 0.8 * alpha);
  cairo_fill(cr);
}

// A pushpin: a round head, a collar and a needle. Tilted when the window is not pinned, upright (pushed in)
// when it is.
void glyph_pin(cairo_t* cr, double cx, double cy, double s, bool pinned, double alpha, const Ink& ink) {
  cairo_save(cr);
  cairo_translate(cr, cx, cy);
  cairo_rotate(cr, pinned ? 0.0 : 0.62);
  const double u = s / 12.0;
  // needle
  cairo_new_path(cr);
  cairo_move_to(cr, 0, 1.5 * u);
  cairo_line_to(cr, 0, 6.2 * u);
  outlined(cr, false, std::max(1.0, 0.9 * u), alpha, ink);
  // collar
  cairo_new_path(cr);
  cairo_move_to(cr, -3.4 * u, 1.5 * u);
  cairo_line_to(cr, 3.4 * u, 1.5 * u);
  outlined(cr, false, std::max(1.5, 1.7 * u), alpha, ink);
  // head
  cairo_new_path(cr);
  cairo_move_to(cr, -2.2 * u, 1.0 * u);
  cairo_line_to(cr, -1.6 * u, -4.4 * u);
  cairo_curve_to(cr, -1.2 * u, -5.8 * u, 1.2 * u, -5.8 * u, 1.6 * u, -4.4 * u);
  cairo_line_to(cr, 2.2 * u, 1.0 * u);
  cairo_close_path(cr);
  outlined(cr, true, 0.6, alpha, ink);
  cairo_restore(cr);
}

void draw_glow(cairo_t* cr, const CaptionButton& b, const Rgb& c, double pressed_scale) {
  const double cx = b.x + b.w / 2, cy = b.y + b.h / 2, r = std::max(b.w, b.h) * 0.95;
  cairo_pattern_t* p = cairo_pattern_create_radial(cx, cy, std::min(b.w, b.h) * 0.25, cx, cy, r);
  cairo_pattern_add_color_stop_rgba(p, 0.0, c.r, c.g, c.b, 0.85 * pressed_scale);
  cairo_pattern_add_color_stop_rgba(p, 0.55, c.r, c.g, c.b, 0.40 * pressed_scale);
  cairo_pattern_add_color_stop_rgba(p, 1.0, c.r, c.g, c.b, 0.0);
  cairo_set_source(cr, p);
  cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
  cairo_fill(cr);
  cairo_pattern_destroy(p);
}

}  // namespace

void draw_caption_buttons(cairo_t* cr, const CaptionButton* buttons, int count, const CaptionState& st) {
  if (count < 1) return;
  const Rgb bg = rgb_of(st.colors.bg), fg = rgb_of(st.colors.fg), accent = rgb_of(st.colors.accent);
  const bool dark = luma(bg) < 0.5;
  const double focus = st.focused ? 1.0 : 0.62;  // an inactive window's buttons fade
  // Glass mode: the buttons are translucent like the bar they sit on (close stays nearly solid).
  const double neutral_alpha = (st.glass ? 0.80 : 1.0) * focus, red_alpha = (st.glass ? 0.92 : 1.0) * focus;
  const double alpha = focus;
  double x0 = buttons[0].x, x1 = buttons[0].x + buttons[0].w, y0 = buttons[0].y, y1 = buttons[0].y + buttons[0].h;
  for (int i = 1; i < count; ++i) {
    x0 = std::min(x0, buttons[i].x);
    x1 = std::max(x1, buttons[i].x + buttons[i].w);
    y0 = std::min(y0, buttons[i].y);
    y1 = std::max(y1, buttons[i].y + buttons[i].h);
  }
  const double h = y1 - y0, radius = std::clamp(h * 0.22, 2.0, 5.5);
  const double shine = dark ? 0.30 : 0.55;  // strength of the white bevel along the top and left edges

  cairo_save(cr);
  // The glow of the hovered button first, so the strip is drawn over it: the accent colour, orange behind close.
  for (int i = 0; i < count; ++i)
    if (buttons[i].id == st.hover_id || buttons[i].id == st.pressed_id)
      draw_glow(cr, buttons[i], buttons[i].id == kClose ? kGlowRed : lerp(accent, kWhite, 0.15), buttons[i].id == st.pressed_id ? 0.55 : 1.0);

  strip_path(cr, x0, y0, x1 - x0, h, radius, radius);
  cairo_clip(cr);
  Glass glasses[4];
  for (int i = 0; i < count; ++i) {
    const CaptionButton& b = buttons[i];
    const bool toggled = b.id == kPin && st.pinned;
    glasses[i] = glass_for(b.id, b.id == st.hover_id, b.id == st.pressed_id, toggled, st, dark);
    fill_glass(cr, glasses[i], b.x, b.y, b.w, b.h, b.id == kClose ? red_alpha : neutral_alpha);
    // bevel: a light edge along the top and the left of the glass, a soft shade at the bottom
    cairo_set_source_rgba(cr, 1, 1, 1, shine * alpha);
    cairo_rectangle(cr, b.x + 1, b.y, b.w - 1, 1);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, shine * 0.7 * alpha);
    cairo_rectangle(cr, b.x + 1, b.y, 1, b.h);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.12 * alpha);
    cairo_rectangle(cr, b.x, b.y + b.h - 2, b.w, 1);
    cairo_fill(cr);
  }
  // separators between neighbours: a dark line with a light one next to it
  const Rgb line = lerp(bg, kBlack, dark ? 0.70 : 0.50);
  for (int i = 1; i < count; ++i) {
    const double sx = buttons[i].x;
    const bool red = buttons[i].id == kClose || buttons[i - 1].id == kClose;
    rgba(cr, red ? lerp(kRed, kBlack, 0.7) : line, 0.62 * alpha);
    cairo_rectangle(cr, sx - 0.5, y0, 1, h);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, shine * 0.75 * alpha);
    cairo_rectangle(cr, sx + 0.5, y0, 1, h);
    cairo_fill(cr);
  }
  cairo_reset_clip(cr);

  // glyphs: the theme's text colour with a contrasting outline on the glass, white on the red close button
  const bool fg_light = luma(fg) > 0.5;
  for (int i = 0; i < count; ++i) {
    const CaptionButton& b = buttons[i];
    const double cx = b.x + b.w / 2, cy = b.y + b.h / 2 - 0.5, s = std::min(b.h * 0.5, 12.0);
    Ink ink;
    if (b.id == kClose) {
      ink = {kWhite, lerp(kRed, kBlack, 0.78), lerp(glasses[i].lower_start, kBlack, 0.1)};
    } else {
      ink = {fg, lerp(bg, fg_light ? kBlack : kWhite, fg_light ? 0.72 : 0.85), lerp(glasses[i].lower_start, kBlack, 0.15)};
    }
    switch (b.id) {
      case kClose: glyph_close(cr, cx, cy, s, alpha, ink); break;
      case kMinimize: glyph_minimize(cr, cx, cy, s, alpha, ink); break;
      case kMaximize: (st.maximized ? glyph_restore : glyph_maximize)(cr, cx, cy, s, alpha, ink); break;
      case kPin: glyph_pin(cr, cx, cy + 0.5, s * 1.15, st.pinned, alpha, ink); break;
      default: break;
    }
  }

  // outline of the strip: dark on the left, bottom and right (its top edge is the edge of the window)
  cairo_rectangle(cr, x0 - 1, y0, x1 - x0 + 2, h + 1);  // nothing above the top edge: that is the window's edge
  cairo_clip(cr);
  strip_path(cr, x0 + 0.5, y0 - 1, x1 - x0 - 1, h + 0.5, radius, radius);
  rgba(cr, line, 0.85 * alpha);
  cairo_set_line_width(cr, 1);
  cairo_stroke(cr);
  cairo_restore(cr);
}

}  // namespace fleetwm::kit
