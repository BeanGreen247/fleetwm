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
#include "window_geometry.hpp"

namespace fleetwm {

namespace {

namespace kit = fleetwm::kit;

kit::Palette g_palette;

// A wlr_buffer over a plain malloc'd ARGB8888 pixel array.
struct PixelBuffer {
  wlr_buffer base;
  void* data;
  size_t stride;
};

void pixel_buffer_destroy(wlr_buffer* buffer) {
  PixelBuffer* pb = wl_container_of(buffer, pb, base);
  std::free(pb->data);
  delete pb;
}

bool pixel_buffer_begin_access(wlr_buffer* buffer, uint32_t flags, void** data, uint32_t* format,
                               size_t* stride) {
  if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE) return false;
  PixelBuffer* pb = wl_container_of(buffer, pb, base);
  *data = pb->data;
  *format = DRM_FORMAT_ARGB8888;
  *stride = pb->stride;
  return true;
}

void pixel_buffer_end_access(wlr_buffer*) {}

const wlr_buffer_impl kPixelBufferImpl = {
    pixel_buffer_destroy, nullptr, nullptr, pixel_buffer_begin_access, pixel_buffer_end_access,
};

kit::Color mix(const kit::Color& a, const kit::Color& b, double t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0};
}

}  // namespace

void titlebar_reload_palette(const ThemeConfig& theme) { g_palette = kit::load_palette(theme); }

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
  auto* pb = new PixelBuffer;
  pb->stride = static_cast<size_t>(width) * 4;
  pb->data = std::calloc(static_cast<size_t>(height), pb->stride);
  if (!pb->data) {
    delete pb;
    return nullptr;
  }
  wlr_buffer_init(&pb->base, &kPixelBufferImpl, width, height);

  cairo_surface_t* surf = cairo_image_surface_create_for_data(
      static_cast<unsigned char*>(pb->data), CAIRO_FORMAT_ARGB32, width, height,
      static_cast<int>(pb->stride));
  cairo_t* cr = cairo_create(surf);

  const kit::Palette& pal = g_palette;
  const kit::Color bg = st.focused ? mix(pal.bg_secondary, pal.accent, 0.12) : pal.bg_secondary;
  const kit::Color fg = st.focused ? pal.fg_primary : pal.fg_secondary;

  kit::set_source(cr, bg);
  cairo_paint(cr);

  // Hairline under the bar separates it from the window content.
  kit::set_source(cr, mix(bg, pal.fg_primary, 0.12));
  cairo_rectangle(cr, 0, height - 1, width, 1);
  cairo_fill(cr);

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
    const bool lit_pin = which == geom::kBtnPin && st.pinned;
    if (hot || lit_pin) {
      const kit::Color fill = which == geom::kBtnClose ? kit::Color{0.90, 0.28, 0.30, 1.0}
                              : lit_pin && !hot        ? mix(bg, pal.accent, 0.35)
                                                       : mix(bg, pal.fg_primary, 0.18);
      kit::set_source(cr, fill);
      kit::rounded_rect(cr, slot.x + 2, slot.y, slot.w - 4, slot.h, std::min(slot.h / 2.0, 8.0));
      cairo_fill(cr);
      if (which == geom::kBtnClose && hot) glyph = {1, 1, 1, 1};
      if (lit_pin) glyph = pal.accent;
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
  return &pb->base;
}

}  // namespace fleetwm
