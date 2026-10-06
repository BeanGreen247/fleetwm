#include "titlebar.hpp"

#include <cairo.h>
#include <drm_fourcc.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <wlr/interfaces/wlr_buffer.h>
}

#include "fleetkit.hpp"
#include "pixel_buffer.hpp"
#include "window_geometry.hpp"

namespace fleetwm {

namespace {

namespace kit = fleetwm::kit;

kit::Palette g_palette;

kit::Color mix(const kit::Color& a, const kit::Color& b, double t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0};
}

}  // namespace

void titlebar_reload_palette(const ThemeConfig& theme) { g_palette = kit::load_palette(theme); }

void titlebar_backdrop_color(float rgba[4]) {
  rgba[0] = static_cast<float>(g_palette.bg_primary.r);
  rgba[1] = static_cast<float>(g_palette.bg_primary.g);
  rgba[2] = static_cast<float>(g_palette.bg_primary.b);
  rgba[3] = 1.0f;
}

geom::TitlebarMetrics titlebar_metrics(const TitlebarConfig& cfg) {
  geom::TitlebarMetrics m;
  m.height = cfg.height;
  m.button_w = cfg.button_width;
  m.button_h = cfg.button_height;
  m.buttons_right = cfg.buttons_side == ButtonSide::Right;
  m.align = cfg.title_align == TitleAlign::Left    ? geom::TitleAlignment::Left
            : cfg.title_align == TitleAlign::Right ? geom::TitleAlignment::Right
                                                   : geom::TitleAlignment::Center;
  m.show_pin = cfg.show_pin;
  m.show_minimize = cfg.show_minimize;
  m.show_maximize = cfg.show_maximize;
  return m;
}

wlr_buffer* render_titlebar(int width, const TitlebarState& st, const TitlebarConfig& cfg) {
  if (width < 1) return nullptr;
  const geom::TitlebarMetrics metrics = titlebar_metrics(cfg);
  const geom::TitlebarLayout layout = geom::layout_titlebar(width, metrics);
  const int height = std::max(16, metrics.height);
  void* pixels = nullptr;
  size_t stride = 0;
  wlr_buffer* buffer = create_pixel_buffer(width, height, &pixels, &stride);
  if (!buffer) return nullptr;

  cairo_surface_t* surf = cairo_image_surface_create_for_data(
      static_cast<unsigned char*>(pixels), CAIRO_FORMAT_ARGB32, width, height, static_cast<int>(stride));
  cairo_t* cr = cairo_create(surf);

  const kit::Palette& pal = g_palette;
  const kit::Color bg = st.focused ? mix(pal.bg_secondary, pal.accent, 0.12) : pal.bg_secondary;
  const kit::Color fg = st.focused ? pal.fg_primary : pal.fg_secondary;

  if (st.glass) {
    // Glass: the theme colour, see-through, with a sheen fading down from the top and one soft
    // diagonal band; a light line along the top edge and a darker one under the bar.
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, bg.r, bg.g, bg.b, st.focused ? 0.74 : 0.56);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_pattern_t* sheen = cairo_pattern_create_linear(0, 0, 0, height);
    cairo_pattern_add_color_stop_rgba(sheen, 0, 1, 1, 1, st.focused ? 0.26 : 0.14);
    cairo_pattern_add_color_stop_rgba(sheen, 0.55, 1, 1, 1, 0.04);
    cairo_pattern_add_color_stop_rgba(sheen, 1, 1, 1, 1, 0.0);
    cairo_set_source(cr, sheen);
    cairo_paint(cr);
    cairo_pattern_destroy(sheen);
    const double bx = width * 0.22, bw = std::max(30.0, width * 0.10), slant = height * 0.6;
    cairo_pattern_t* band = cairo_pattern_create_linear(bx, 0, bx + bw, 0);
    cairo_pattern_add_color_stop_rgba(band, 0, 1, 1, 1, 0.0);
    cairo_pattern_add_color_stop_rgba(band, 0.5, 1, 1, 1, st.focused ? 0.09 : 0.05);
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
  } else {
    kit::set_source(cr, bg);
    cairo_paint(cr);

    // Hairline under the bar separates it from the window content.
    kit::set_source(cr, mix(bg, pal.fg_primary, 0.12));
    cairo_rectangle(cr, 0, height - 1, width, 1);
    cairo_fill(cr);
  }

  // Title, ellipsized to the span the buttons leave free.
  const double font = std::clamp(height * 0.4, 11.0, 16.0);
  const double avail = layout.title_x1 - layout.title_x0;
  std::string text = st.title;
  if (avail > 20 && !text.empty()) {
    bool cut = false;
    while (!text.empty() &&
           kit::measure_text(cr, cut ? text + "..." : text, font, st.focused).width > avail) {
      // Drop one UTF-8 code point (continuation bytes are 10xxxxxx).
      size_t end = text.size() - 1;
      while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
      text.erase(end);
      cut = true;
    }
    if (cut) text += "...";
    const kit::TextExtents te = kit::measure_text(cr, text, font, st.focused);
    const double x = geom::title_x(layout, te.width, metrics.align, width);
    kit::draw_text(cr, text, x, (height - te.height) / 2.0 + te.ascent - 0.5, font, fg, st.focused);
  }

  // Buttons.
  for (int i = 0; i < layout.count; ++i) {
    const geom::ButtonSlot& slot = layout.buttons[i];
    const int which = slot.id;
    const double cx = slot.x + slot.w / 2.0, cy = slot.y + slot.h / 2.0;
    const double glyph_r = std::clamp(std::min(slot.w, slot.h) * 0.17, 3.0, 7.0);
    kit::Color glyph = fg;
    const bool hot = st.hover_button == which;
    if (st.glass) {
      // Round glossy buttons (our own shape: separate orbs, not a joined strip). A faint disc at rest, a
      // lit one on hover, coral for close.
      const double rr = std::min(slot.w, slot.h) * 0.36;
      const bool close = which == geom::kBtnClose;
      const kit::Color base = close ? kit::Color{0.90, 0.35, 0.33, 1.0} : pal.accent;
      cairo_pattern_t* orb = cairo_pattern_create_linear(0, cy - rr, 0, cy + rr);
      const double top_a = hot ? 0.95 : 0.30, bot_a = hot ? 0.70 : 0.12;
      cairo_pattern_add_color_stop_rgba(orb, 0, std::min(1.0, base.r + 0.25), std::min(1.0, base.g + 0.25), std::min(1.0, base.b + 0.25), top_a);
      cairo_pattern_add_color_stop_rgba(orb, 1, base.r * 0.8, base.g * 0.8, base.b * 0.8, bot_a);
      cairo_arc(cr, cx, cy, rr, 0, 2 * M_PI);
      cairo_set_source(cr, orb);
      cairo_fill_preserve(cr);
      cairo_pattern_destroy(orb);
      cairo_set_source_rgba(cr, 1, 1, 1, hot ? 0.75 : 0.35);
      cairo_set_line_width(cr, 1);
      cairo_stroke(cr);
      // a small highlight on the upper half of the orb
      cairo_arc(cr, cx, cy - rr * 0.35, rr * 0.55, M_PI, 2 * M_PI);
      cairo_set_source_rgba(cr, 1, 1, 1, hot ? 0.28 : 0.14);
      cairo_fill(cr);
      if (hot && close) glyph = {1, 1, 1, 1};
    } else if (hot) {
      const kit::Color fill = which == geom::kBtnClose ? kit::Color{0.90, 0.28, 0.30, 1.0}
                                                       : mix(bg, pal.fg_primary, 0.18);
      kit::set_source(cr, fill);
      kit::rounded_rect(cr, slot.x + 2, slot.y, slot.w - 4, slot.h, std::min(slot.h / 2.0, 8.0));
      cairo_fill(cr);
      if (which == geom::kBtnClose) glyph = {1, 1, 1, 1};
    }
    kit::set_source(cr, glyph);
    cairo_set_line_width(cr, 1.4);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    const double r = glyph_r;
    if (which == geom::kBtnClose) {
      cairo_move_to(cr, cx - r, cy - r);
      cairo_line_to(cr, cx + r, cy + r);
      cairo_move_to(cr, cx + r, cy - r);
      cairo_line_to(cr, cx - r, cy + r);
      cairo_stroke(cr);
    } else if (which == geom::kBtnMinimize) {
      cairo_move_to(cr, cx - r, cy + r);
      cairo_line_to(cr, cx + r, cy + r);
      cairo_stroke(cr);
    } else if (which == geom::kBtnPin) {
      // A pushpin: head, shaft and point (filled when the window is pinned).
      cairo_move_to(cr, cx - r * 0.7, cy - r);
      cairo_line_to(cr, cx + r * 0.7, cy - r);
      cairo_line_to(cr, cx + r * 0.4, cy);
      cairo_line_to(cr, cx - r * 0.4, cy);
      cairo_close_path(cr);
      if (st.pinned) cairo_fill_preserve(cr);
      cairo_stroke(cr);
      cairo_move_to(cr, cx - r * 0.9, cy);
      cairo_line_to(cr, cx + r * 0.9, cy);
      cairo_move_to(cr, cx, cy);
      cairo_line_to(cr, cx, cy + r);
      cairo_stroke(cr);
    } else if (st.maximized) {
      cairo_rectangle(cr, cx - r, cy - r * 0.5, r * 1.5, r * 1.5);  // restore: two overlapping squares
      cairo_stroke(cr);
      cairo_move_to(cr, cx - r * 0.5, cy - r);
      cairo_line_to(cr, cx + r, cy - r);
      cairo_line_to(cr, cx + r, cy + r * 0.5);
      cairo_stroke(cr);
    } else {
      cairo_rectangle(cr, cx - r, cy - r, 2 * r, 2 * r);
      cairo_stroke(cr);
    }
  }

  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return buffer;
}

}  // namespace fleetwm
