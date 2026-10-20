#include "glass_backdrop.hpp"

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
struct Kept {
  wlr_buffer* buffer = nullptr;
  cairo_surface_t* source = nullptr;  // the blurred wallpaper it was made from; a different one means the wallpaper changed
};
std::map<std::pair<int, int>, Kept>& kept() {
  static std::map<std::pair<int, int>, Kept> m;
  return m;
}
}  // namespace

wlr_buffer* glass_backdrop_for(int out_w, int out_h) {
  if (out_w < 1 || out_h < 1) return nullptr;
  cairo_surface_t* backdrop = kit::load_backdrop();  // one reference; the picture itself is shared and re-checked every few seconds
  if (!backdrop) return nullptr;
  Kept& k = kept()[{out_w, out_h}];
  if (k.buffer && k.source == backdrop) {
    cairo_surface_destroy(backdrop);
    return wlr_buffer_lock(k.buffer);
  }
  if (k.buffer) wlr_buffer_drop(k.buffer);
  if (k.source) cairo_surface_destroy(k.source);  // the cache keeps a reference, so a new wallpaper can never reuse its address
  k.buffer = nullptr;
  k.source = nullptr;
  void* pixels = nullptr;
  size_t stride = 0;
  wlr_buffer* buffer = create_pixel_buffer(out_w, out_h, &pixels, &stride);
  if (buffer) {
    cairo_surface_t* surf = cairo_image_surface_create_for_data(static_cast<unsigned char*>(pixels), CAIRO_FORMAT_ARGB32, out_w,
                                                                out_h, static_cast<int>(stride));
    cairo_t* cr = cairo_create(surf);
    kit::paint_backdrop(cr, backdrop, out_w, out_h, 0, 0, 0, 0, out_w, out_h);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    k.buffer = buffer;  // the cache owns this reference
    k.source = backdrop;  // and this one
    return wlr_buffer_lock(buffer);
  }
  cairo_surface_destroy(backdrop);
  return nullptr;
}

void glass_backdrop_clear() {
  for (auto& [size, k] : kept())
    {
      if (k.buffer) wlr_buffer_drop(k.buffer);
      if (k.source) cairo_surface_destroy(k.source);
    }
  kept().clear();
}

}  // namespace fleetwm
