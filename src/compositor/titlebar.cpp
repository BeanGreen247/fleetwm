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

#include "caption_buttons.hpp"
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
  m.strip = true;  // the joined Windows 7 style caption strip, drawn by kit::draw_caption_buttons
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

  // Buttons: one joined Windows 7 style strip (pin, minimize, maximize, close), see fleetkit's caption_buttons.hpp.
  kit::CaptionButton strip[4];
  for (int i = 0; i < layout.count; ++i) {
    const geom::ButtonSlot& slot = layout.buttons[i];
    strip[i] = {slot.id, slot.x, slot.y, slot.w, slot.h};
  }
  kit::CaptionState caption;
  caption.focused = st.focused;
  caption.maximized = st.maximized;
  caption.pinned = st.pinned;
  caption.hover_id = st.hover_button;
  kit::draw_caption_buttons(cr, strip, layout.count, caption);

  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return buffer;
}

}  // namespace fleetwm
