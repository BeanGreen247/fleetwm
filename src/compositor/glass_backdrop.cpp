#include "glass_backdrop.hpp"

#include <cairo.h>

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
Kept& kept() {
  static Kept k;
  return k;
}
}  // namespace

wlr_buffer* glass_backdrop() {
  cairo_surface_t* backdrop = kit::load_backdrop();  // one reference; the picture itself is shared and re-checked every few seconds
  if (!backdrop) return nullptr;
  Kept& k = kept();
  if (k.buffer && k.source == backdrop) {
    cairo_surface_destroy(backdrop);
    return wlr_buffer_lock(k.buffer);
  }
  if (k.buffer) wlr_buffer_drop(k.buffer);
  if (k.source) cairo_surface_destroy(k.source);  // the cache keeps a reference, so a new wallpaper can never reuse its address
  k.buffer = nullptr;
  k.source = nullptr;
  const int bw = cairo_image_surface_get_width(backdrop), bh = cairo_image_surface_get_height(backdrop);
  void* pixels = nullptr;
  size_t stride = 0;
  wlr_buffer* buffer = create_pixel_buffer(bw, bh, &pixels, &stride);
  if (buffer) {
    cairo_surface_t* surf = cairo_image_surface_create_for_data(static_cast<unsigned char*>(pixels), CAIRO_FORMAT_ARGB32, bw, bh,
                                                                static_cast<int>(stride));
    cairo_t* cr = cairo_create(surf);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);  // the picture is RGB24: this makes the alpha opaque
    cairo_set_source_surface(cr, backdrop, 0, 0);
    cairo_paint(cr);
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
  Kept& k = kept();
  if (k.buffer) wlr_buffer_drop(k.buffer);
  if (k.source) cairo_surface_destroy(k.source);
  k = Kept{};
}

}  // namespace fleetwm
