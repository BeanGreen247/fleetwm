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

int titlebar_button_at(int width, double x) {
  return geom::titlebar_button_at(width, x, kTitlebarButtonWidth);
}

wlr_buffer* render_titlebar(int width, const std::string& title, bool focused, bool maximized,
                            int hover_button) {
  if (width < 1) return nullptr;
  const int height = kTitlebarHeight;
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
  const kit::Color bg = focused ? mix(pal.bg_secondary, pal.accent, 0.12) : pal.bg_secondary;
  const kit::Color fg = focused ? pal.fg_primary : pal.fg_secondary;

  kit::set_source(cr, bg);
  cairo_paint(cr);

  // Hairline under the bar separates it from the window content.
  kit::set_source(cr, mix(bg, pal.fg_primary, 0.12));
  cairo_rectangle(cr, 0, height - 1, width, 1);
  cairo_fill(cr);

  // Title: centered, ellipsized to the room between the margins and buttons.
  const double avail = width - 2 * (3 * kTitlebarButtonWidth) - 8;
  std::string text = title;
  if (avail > 20 && !text.empty()) {
    bool cut = false;
    while (!text.empty() &&
           kit::measure_text(cr, cut ? text + "..." : text, 13, focused).width > avail) {
      // Drop one UTF-8 code point (continuation bytes are 10xxxxxx).
      size_t end = text.size() - 1;
      while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
      text.erase(end);
      cut = true;
    }
    if (cut) text += "...";
    const kit::TextExtents te = kit::measure_text(cr, text, 13, focused);
    const double x = std::max(8.0, (width - te.width) / 2.0);
    kit::draw_text(cr, text, x, (height - te.height) / 2.0 + te.ascent - 0.5, 13, fg, focused);
  }

  // Buttons.
  auto button_center = [&](int which) {
    const int idx = which == kButtonClose ? 0 : which == kButtonMaximize ? 1 : 2;
    return width - (idx + 0.5) * kTitlebarButtonWidth;
  };
  const double cy = (height - 1) / 2.0;
  for (int which : {kButtonMinimize, kButtonMaximize, kButtonClose}) {
    const double cx = button_center(which);
    kit::Color glyph = fg;
    if (hover_button == which) {
      const kit::Color hot = which == kButtonClose ? kit::Color{0.90, 0.28, 0.30, 1.0}
                                                   : mix(bg, pal.fg_primary, 0.18);
      kit::set_source(cr, hot);
      cairo_arc(cr, cx, cy, 11, 0, 2 * M_PI);
      cairo_fill(cr);
      if (which == kButtonClose) glyph = {1, 1, 1, 1};
    }
    kit::set_source(cr, glyph);
    cairo_set_line_width(cr, 1.4);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    if (which == kButtonClose) {
      cairo_move_to(cr, cx - 4, cy - 4);
      cairo_line_to(cr, cx + 4, cy + 4);
      cairo_move_to(cr, cx + 4, cy - 4);
      cairo_line_to(cr, cx - 4, cy + 4);
      cairo_stroke(cr);
    } else if (which == kButtonMinimize) {
      cairo_move_to(cr, cx - 4, cy + 4);
      cairo_line_to(cr, cx + 4, cy + 4);
      cairo_stroke(cr);
    } else if (maximized) {
      cairo_rectangle(cr, cx - 4, cy - 2, 6, 6);  // restore: two overlapping squares
      cairo_stroke(cr);
      cairo_move_to(cr, cx - 2, cy - 4);
      cairo_line_to(cr, cx + 4, cy - 4);
      cairo_line_to(cr, cx + 4, cy + 2);
      cairo_stroke(cr);
    } else {
      cairo_rectangle(cr, cx - 4, cy - 4, 8, 8);
      cairo_stroke(cr);
    }
  }

  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return &pb->base;
}

}  // namespace fleetwm
