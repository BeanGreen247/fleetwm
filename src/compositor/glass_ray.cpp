#include "glass_ray.hpp"

#include <cairo.h>

#include <map>
#include <utility>

extern "C" {
#include <wlr/types/wlr_buffer.h>
}

#include "backdrop.hpp"
#include "pixel_buffer.hpp"

namespace fleetwm {

namespace {
std::map<std::pair<int, int>, wlr_buffer*>& ray_kept() {
  static std::map<std::pair<int, int>, wlr_buffer*> m;
  return m;
}
}  // namespace

int glass_ray_margin(int out_w) { return out_w * 12 / 100; }

wlr_buffer* glass_ray_for(int out_w, int out_h) {
  if (out_w < 100 || out_h < 100) return nullptr;
  wlr_buffer*& slot = ray_kept()[{out_w, out_h}];
  if (!slot) {
    const int margin = glass_ray_margin(out_w);
    const int w = (out_w + 2 * margin) / kRayScale, h = out_h / kRayScale;
    void* pixels = nullptr;
    size_t stride = 0;
    wlr_buffer* b = create_pixel_buffer(w, h, &pixels, &stride);
    if (!b) return nullptr;
    cairo_surface_t* surf = cairo_image_surface_create_for_data(static_cast<unsigned char*>(pixels), CAIRO_FORMAT_ARGB32, w, h, static_cast<int>(stride));
    cairo_t* cr = cairo_create(surf);
    cairo_scale(cr, 1.0 / kRayScale, 1.0 / kRayScale);
    cairo_translate(cr, margin, 0);  // output coordinates from here on; the picture reaches `margin` pixels beyond the output on each side
    kit::paint_screen_ray(cr, -margin, 0, -margin, 0, out_w + 2 * margin, out_h, out_w, out_h, 1.0);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    slot = b;  // this process keeps the producer's reference
  }
  return wlr_buffer_lock(slot);
}

void glass_ray_clear() {
  for (auto& [size, b] : ray_kept()) wlr_buffer_drop(b);
  ray_kept().clear();
}

}  // namespace fleetwm
