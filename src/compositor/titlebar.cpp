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

#include "titlebar_draw.hpp"
#include "fleetkit.hpp"
#include "pixel_buffer.hpp"
#include "window_geometry.hpp"

namespace fleetwm {

namespace {

namespace kit = fleetwm::kit;

kit::Palette g_palette;

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

  kit::TitlebarPaint paint;
  paint.title = st.title;
  paint.focused = st.focused;
  paint.maximized = st.maximized;
  paint.pinned = st.pinned;
  paint.glass = st.glass;
  paint.hover_id = st.hover_button;
  paint.layout = layout;
  paint.align = metrics.align;
  kit::draw_titlebar(cr, width, height, paint, g_palette);

  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return buffer;
}

}  // namespace fleetwm
