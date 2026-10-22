#include "window_shadow.hpp"

#include "box_blur.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace fleetwm::kit {

namespace {

void box_blur(unsigned char* p, int w, int h, int stride, int r, int passes) { box_blur_u8(p, w, h, stride, 1, r, passes); }

}  // namespace

void draw_window_shadow(cairo_t* cr, bool focused) {
  const int n = kShadowSize;
  cairo_surface_t* mask = cairo_image_surface_create(CAIRO_FORMAT_A8, n, n);
  cairo_t* mc = cairo_create(mask);
  cairo_set_source_rgba(mc, 0, 0, 0, 1);
  const double x0 = kShadowReach, y0 = kShadowReach, x1 = n - kShadowReach, y1 = n - kShadowReach, r = 6;
  cairo_new_sub_path(mc);
  cairo_arc(mc, x1 - r, y0 + r, r, -M_PI / 2, 0);
  cairo_arc(mc, x1 - r, y1 - r, r, 0, M_PI / 2);
  cairo_arc(mc, x0 + r, y1 - r, r, M_PI / 2, M_PI);
  cairo_arc(mc, x0 + r, y0 + r, r, M_PI, 3 * M_PI / 2);
  cairo_close_path(mc);
  cairo_fill(mc);
  cairo_destroy(mc);
  cairo_surface_flush(mask);
  // Three box passes approximate a gaussian; a focused window's shadow spreads wider.
  box_blur(cairo_image_surface_get_data(mask), n, n, cairo_image_surface_get_stride(mask), focused ? 8 : 6, 3);
  cairo_surface_mark_dirty(mask);
  cairo_set_source_rgba(cr, 0, 0, 0, focused ? 0.55 : 0.34);
  cairo_mask_surface(cr, mask, 0, 0);
  cairo_surface_destroy(mask);
  // Nothing under the window itself: the frame is translucent, and the corner pieces of the compositor reach into the window's area.
  cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OUT);
  cairo_new_sub_path(cr);
  cairo_arc(cr, x1 - r, y0 + r, r, -M_PI / 2, 0);
  cairo_arc(cr, x1 - r, y1 - r, r, 0, M_PI / 2);
  cairo_arc(cr, x0 + r, y1 - r, r, M_PI / 2, M_PI);
  cairo_arc(cr, x0 + r, y0 + r, r, M_PI, 3 * M_PI / 2);
  cairo_close_path(cr);
  cairo_set_source_rgba(cr, 0, 0, 0, 1);
  cairo_fill(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
}

}  // namespace fleetwm::kit
