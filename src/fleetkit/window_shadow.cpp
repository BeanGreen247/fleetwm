#include "window_shadow.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace fleetwm::kit {

namespace {

void box_blur(unsigned char* p, int w, int h, int stride, int r, int passes) {
  std::vector<unsigned char> tmp(static_cast<size_t>(stride) * h);
  const int div = 2 * r + 1;
  for (int pass = 0; pass < passes; ++pass) {
    for (int y = 0; y < h; ++y) {
      const unsigned char* s = p + static_cast<size_t>(y) * stride;
      unsigned char* d = &tmp[static_cast<size_t>(y) * stride];
      int sum = 0;
      for (int x = -r; x <= r; ++x) sum += s[std::clamp(x, 0, w - 1)];
      for (int x = 0; x < w; ++x) {
        d[x] = static_cast<unsigned char>(sum / div);
        sum += s[std::min(x + r + 1, w - 1)] - s[std::max(x - r, 0)];
      }
    }
    for (int x = 0; x < w; ++x) {
      int sum = 0;
      for (int y = -r; y <= r; ++y) sum += tmp[static_cast<size_t>(std::clamp(y, 0, h - 1)) * stride + x];
      for (int y = 0; y < h; ++y) {
        p[static_cast<size_t>(y) * stride + x] = static_cast<unsigned char>(sum / div);
        sum += tmp[static_cast<size_t>(std::min(y + r + 1, h - 1)) * stride + x] - tmp[static_cast<size_t>(std::max(y - r, 0)) * stride + x];
      }
    }
  }
}

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
