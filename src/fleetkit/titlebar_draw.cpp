#include "titlebar_draw.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "caption_buttons.hpp"

namespace fleetwm::kit {

namespace {

Color tb_mix(const Color& a, const Color& b, double t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0};
}

// The glass background, drawn from scratch: the theme colour see-through, a sheen fading down from the top
// and one soft diagonal band; a light line along the top edge and a darker one under the bar.
void draw_glass_background(cairo_t* cr, int width, int height, bool focused, const Color& bg) {
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr, bg.r, bg.g, bg.b, focused ? 0.74 : 0.56);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  cairo_pattern_t* sheen = cairo_pattern_create_linear(0, 0, 0, height);
  cairo_pattern_add_color_stop_rgba(sheen, 0, 1, 1, 1, focused ? 0.26 : 0.14);
  cairo_pattern_add_color_stop_rgba(sheen, 0.55, 1, 1, 1, 0.04);
  cairo_pattern_add_color_stop_rgba(sheen, 1, 1, 1, 1, 0.0);
  cairo_set_source(cr, sheen);
  cairo_paint(cr);
  cairo_pattern_destroy(sheen);
  const double bx = width * 0.22, bw = std::max(30.0, width * 0.10), slant = height * 0.6;
  cairo_pattern_t* band = cairo_pattern_create_linear(bx, 0, bx + bw, 0);
  cairo_pattern_add_color_stop_rgba(band, 0, 1, 1, 1, 0.0);
  cairo_pattern_add_color_stop_rgba(band, 0.5, 1, 1, 1, focused ? 0.09 : 0.05);
  cairo_pattern_add_color_stop_rgba(band, 1, 1, 1, 1, 0.0);
  cairo_set_source(cr, band);
  cairo_move_to(cr, bx + slant, 0);
  cairo_line_to(cr, bx + bw + slant, 0);
  cairo_line_to(cr, bx + bw, height);
  cairo_line_to(cr, bx, height);
  cairo_close_path(cr);
  cairo_fill(cr);
  cairo_pattern_destroy(band);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.30);
  cairo_rectangle(cr, 0, 0, width, 1);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.30);
  cairo_rectangle(cr, 0, height - 1, width, 1);
  cairo_fill(cr);
}

// Finished glass backgrounds. A window's titlebar is redrawn whenever the pointer enters or leaves a
// button, its title changes or it gains or loses focus; the background behind all that depends only on the
// size, the focus and the colour, so it is drawn once and copied. Small, byte-capped, least recently used.
struct GlassEntry {
  int w = 0, h = 0;
  bool focused = false;
  double r = 0, g = 0, b = 0;
  cairo_surface_t* image = nullptr;
  unsigned long used = 0;
};
std::vector<GlassEntry> g_glass;
unsigned long g_glass_clock = 0;
constexpr size_t kGlassBudget = 2u << 20;

size_t entry_bytes(const GlassEntry& e) { return static_cast<size_t>(e.w) * e.h * 4; }

}  // namespace

void paint_titlebar_glass_background(cairo_t* cr, int width, int height, bool focused, const Color& base) {
  for (GlassEntry& e : g_glass)
    if (e.w == width && e.h == height && e.focused == focused && e.r == base.r && e.g == base.g && e.b == base.b) {
      e.used = ++g_glass_clock;
      cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
      cairo_set_source_surface(cr, e.image, 0, 0);
      cairo_paint(cr);
      cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
      return;
    }
  GlassEntry made;
  made.w = width;
  made.h = height;
  made.focused = focused;
  made.r = base.r;
  made.g = base.g;
  made.b = base.b;
  if (entry_bytes(made) <= kGlassBudget / 2) {  // an unreasonably wide bar is just drawn, not kept
    made.image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* ic = cairo_create(made.image);
    draw_glass_background(ic, width, height, focused, base);
    cairo_destroy(ic);
    size_t total = entry_bytes(made);
    for (const GlassEntry& e : g_glass) total += entry_bytes(e);
    while (total > kGlassBudget && !g_glass.empty()) {
      auto lru = std::min_element(g_glass.begin(), g_glass.end(), [](const GlassEntry& a, const GlassEntry& b) { return a.used < b.used; });
      total -= entry_bytes(*lru);
      cairo_surface_destroy(lru->image);
      g_glass.erase(lru);
    }
    made.used = ++g_glass_clock;
    g_glass.push_back(made);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, made.image, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    return;
  }
  draw_glass_background(cr, width, height, focused, base);
}

TitlebarCacheStats titlebar_glass_cache_stats() {
  TitlebarCacheStats s;
  s.entries = static_cast<int>(g_glass.size());
  for (const GlassEntry& e : g_glass) s.bytes += entry_bytes(e);
  return s;
}

void titlebar_glass_cache_clear() {
  for (GlassEntry& e : g_glass) cairo_surface_destroy(e.image);
  g_glass.clear();
}

namespace {

// Finished caption strips. Drawing the strip (glass gradients, bevels, glyphs with outlines, the glow) is
// most of a titlebar redraw, and a titlebar is redrawn every time the pointer enters or leaves a button, so
// the strip is drawn once per state (which button is lit, maximized, pinned, focused) and copied. The picture
// covers the strip plus the room its glow needs sideways, and the bar's height.
struct StripEntry {
  int count = 0, bar_h = 0, margin = 0;
  int ids[4] = {0, 0, 0, 0};
  int geom_[4][4] = {};  // x, y, w, h of each button in 1/4 px
  bool focused = false, maximized = false, pinned = false, glass = false;
  int hover = -1, pressed = -1;
  int colors[9] = {};  // bg, fg and accent as 8-bit channels
  double origin_x = 0;
  cairo_surface_t* image = nullptr;
  unsigned long used = 0;
};
std::vector<StripEntry> g_strips;
unsigned long g_strip_clock = 0;
constexpr size_t kStripBudget = 1u << 20;

int quarter(double v) { return static_cast<int>(std::lround(v * 4)); }

void colors_of(const CaptionState& st, int out[9]) {
  const Color cs[3] = {st.colors.bg, st.colors.fg, st.colors.accent};
  for (int i = 0; i < 3; ++i) {
    out[i * 3] = static_cast<int>(std::lround(cs[i].r * 255));
    out[i * 3 + 1] = static_cast<int>(std::lround(cs[i].g * 255));
    out[i * 3 + 2] = static_cast<int>(std::lround(cs[i].b * 255));
  }
}

bool same_strip(const StripEntry& e, const CaptionButton* b, int count, const CaptionState& st, int bar_h) {
  if (e.count != count || e.bar_h != bar_h || e.focused != st.focused || e.maximized != st.maximized || e.pinned != st.pinned ||
      e.glass != st.glass || e.hover != st.hover_id || e.pressed != st.pressed_id)
    return false;
  int c[9];
  colors_of(st, c);
  for (int i = 0; i < 9; ++i)
    if (e.colors[i] != c[i]) return false;
  for (int i = 0; i < count; ++i)
    if (e.ids[i] != b[i].id || e.geom_[i][0] != quarter(b[i].x) || e.geom_[i][1] != quarter(b[i].y) || e.geom_[i][2] != quarter(b[i].w) ||
        e.geom_[i][3] != quarter(b[i].h))
      return false;
  return true;
}

size_t strip_bytes(const StripEntry& e) {
  return e.image ? static_cast<size_t>(cairo_image_surface_get_width(e.image)) * cairo_image_surface_get_height(e.image) * 4 : 0;
}

void blit_strip(cairo_t* cr, const StripEntry& e) {
  cairo_set_source_surface(cr, e.image, e.origin_x, 0);
  cairo_paint(cr);
}

void draw_strip_cached(cairo_t* cr, const CaptionButton* b, int count, const CaptionState& st, int bar_h, int bar_w) {
  for (StripEntry& e : g_strips)
    if (same_strip(e, b, count, st, bar_h)) {
      e.used = ++g_strip_clock;
      blit_strip(cr, e);
      return;
    }
  double x0 = b[0].x, x1 = b[0].x + b[0].w;
  for (int i = 1; i < count; ++i) {
    x0 = std::min(x0, b[i].x);
    x1 = std::max(x1, b[i].x + b[i].w);
  }
  const bool lit = st.hover_id >= 0 || st.pressed_id >= 0;
  StripEntry e;
  e.count = count;
  e.bar_h = bar_h;
  e.margin = lit ? 64 : 2;  // the glow reaches about this far sideways
  e.focused = st.focused;
  e.maximized = st.maximized;
  e.pinned = st.pinned;
  e.glass = st.glass;
  colors_of(st, e.colors);
  e.hover = st.hover_id;
  e.pressed = st.pressed_id;
  for (int i = 0; i < count; ++i) {
    e.ids[i] = b[i].id;
    e.geom_[i][0] = quarter(b[i].x);
    e.geom_[i][1] = quarter(b[i].y);
    e.geom_[i][2] = quarter(b[i].w);
    e.geom_[i][3] = quarter(b[i].h);
  }
  const double left = std::max(0.0, std::floor(x0) - e.margin), right = std::min<double>(bar_w, std::ceil(x1) + e.margin);
  e.origin_x = left;
  const int iw = std::max(1, static_cast<int>(right - left));
  if (static_cast<size_t>(iw) * bar_h * 4 > kStripBudget / 2) {  // not worth keeping: draw directly
    draw_caption_buttons(cr, b, count, st);
    return;
  }
  e.image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, bar_h);
  cairo_t* ic = cairo_create(e.image);
  cairo_translate(ic, -left, 0);
  draw_caption_buttons(ic, b, count, st);
  cairo_destroy(ic);
  size_t total = strip_bytes(e);
  for (const StripEntry& o : g_strips) total += strip_bytes(o);
  while (total > kStripBudget && !g_strips.empty()) {
    auto lru = std::min_element(g_strips.begin(), g_strips.end(), [](const StripEntry& a, const StripEntry& c) { return a.used < c.used; });
    total -= strip_bytes(*lru);
    cairo_surface_destroy(lru->image);
    g_strips.erase(lru);
  }
  e.used = ++g_strip_clock;
  g_strips.push_back(e);
  blit_strip(cr, g_strips.back());
}

}  // namespace

TitlebarCacheStats titlebar_strip_cache_stats() {
  TitlebarCacheStats s;
  s.entries = static_cast<int>(g_strips.size());
  for (const StripEntry& e : g_strips) s.bytes += strip_bytes(e);
  return s;
}

void titlebar_strip_cache_clear() {
  for (StripEntry& e : g_strips) cairo_surface_destroy(e.image);
  g_strips.clear();
}

void draw_frame_strip(cairo_t* cr, int width, int height, FrameEdge edge, bool focused, bool glass, const Palette& pal,
                      bool round_bottom) {
  const Color bg = focused ? tb_mix(pal.bg_secondary, pal.accent, 0.12) : pal.bg_secondary;
  const bool left = edge == FrameEdge::Left, right = edge == FrameEdge::Right;
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  if (glass) cairo_set_source_rgba(cr, bg.r, bg.g, bg.b, focused ? 0.74 : 0.56);
  else cairo_set_source_rgba(cr, bg.r, bg.g, bg.b, 1.0);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  // Line against the window content ("inner") and along the outside of the window ("outer").
  const Color rim = tb_mix(bg, pal.fg_primary, 0.12);
  auto line = [&](double r, double g, double b, double a, bool outer) {
    cairo_set_source_rgba(cr, r, g, b, a);
    if (left) cairo_rectangle(cr, outer ? 0 : width - 1, 0, 1, height);
    else if (right) cairo_rectangle(cr, outer ? width - 1 : 0, 0, 1, height);
    else cairo_rectangle(cr, 0, outer ? height - 1 : 0, width, 1);
    cairo_fill(cr);
  };
  if (glass) {
    line(1, 1, 1, 0.30, true);
    line(0, 0, 0, 0.30, false);
  } else {
    line(rim.r, rim.g, rim.b, 1.0, true);
  }
  if (round_bottom && edge == FrameEdge::Bottom && width > 4) {
    // Keep only what lies inside a rectangle whose two bottom corners are rounded. The strip is only a few pixels
    // tall, so the radius stops at its height; the sides above it are square and meet it where it is full width.
    const double r = std::min<double>(kWindowCornerRadius, height);
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_DEST_IN);
    cairo_new_path(cr);
    cairo_move_to(cr, 0, 0);
    cairo_line_to(cr, width, 0);
    cairo_line_to(cr, width, height - r);
    cairo_arc(cr, width - r, height - r, r, 0, M_PI / 2);
    cairo_line_to(cr, r, height);
    cairo_arc(cr, r, height - r, r, M_PI / 2, M_PI);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0, 0, 0, 1);
    cairo_fill(cr);
    cairo_restore(cr);
  }
}

void draw_titlebar(cairo_t* cr, int width, int height, const TitlebarPaint& p, const Palette& pal) {
  const Color bg = p.focused ? tb_mix(pal.bg_secondary, pal.accent, 0.12) : pal.bg_secondary;
  const Color fg = p.focused ? pal.fg_primary : pal.fg_secondary;

  if (p.glass) {
    paint_titlebar_glass_background(cr, width, height, p.focused, bg);
  } else {
    set_source(cr, bg);
    cairo_paint(cr);
    // Hairline under the bar separates it from the window content.
    set_source(cr, tb_mix(bg, pal.fg_primary, 0.12));
    cairo_rectangle(cr, 0, height - 1, width, 1);
    cairo_fill(cr);
  }

  // Title, ellipsized to the span the buttons leave free.
  const double font = std::clamp(height * 0.4, 11.0, 16.0);
  const double avail = p.layout.title_x1 - p.layout.title_x0;
  std::string text = p.title;
  if (avail > 20 && !text.empty()) {
    bool cut = false;
    while (!text.empty() && measure_text(cr, cut ? text + "..." : text, font, p.focused).width > avail) {
      // Drop one UTF-8 code point (continuation bytes are 10xxxxxx).
      size_t end = text.size() - 1;
      while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
      text.erase(end);
      cut = true;
    }
    if (cut) text += "...";
    const TextExtents te = measure_text(cr, text, font, p.focused);
    const double x = geom::title_x(p.layout, te.width, p.align, width);
    draw_text(cr, text, x, (height - te.height) / 2.0 + te.ascent - 0.5, font, fg, p.focused);
  }

  // Buttons: one joined Windows 7 style strip (pin, minimize, maximize, close), see caption_buttons.hpp.
  CaptionButton strip[4];
  for (int i = 0; i < p.layout.count; ++i) {
    const geom::ButtonSlot& slot = p.layout.buttons[i];
    strip[i] = {slot.id, slot.x, slot.y, slot.w, slot.h};
  }
  CaptionState caption;
  caption.colors = {bg, fg, pal.accent};  // the strip is made from the titlebar's own theme colours
  caption.glass = p.glass;
  caption.focused = p.focused;
  caption.maximized = p.maximized;
  caption.pinned = p.pinned;
  caption.hover_id = p.hover_id;
  caption.pressed_id = p.pressed_id;
  if (p.layout.count > 0) draw_strip_cached(cr, strip, p.layout.count, caption, height, width);

  if (p.round_top && width > 2 * kWindowCornerRadius) {
    const double r = std::min<double>(kWindowCornerRadius, height);
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_DEST_IN);
    cairo_new_path(cr);
    cairo_move_to(cr, 0, height);
    cairo_line_to(cr, 0, r);
    cairo_arc(cr, r, r, r, M_PI, 3 * M_PI / 2);
    cairo_line_to(cr, width - r, 0);
    cairo_arc(cr, width - r, r, r, 3 * M_PI / 2, 2 * M_PI);
    cairo_line_to(cr, width, height);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0, 0, 0, 1);
    cairo_fill(cr);
    cairo_restore(cr);
  }
}

}  // namespace fleetwm::kit
