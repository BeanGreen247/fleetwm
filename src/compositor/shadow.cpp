#include "shadow.hpp"

#include <cairo.h>

extern "C" {
#include <wlr/types/wlr_buffer.h>
}

#include "pixel_buffer.hpp"
#include "window_shadow.hpp"

namespace fleetwm {

namespace {
wlr_buffer* g_pictures[2] = {};
}

wlr_buffer* shadow_picture(bool focused) {
  wlr_buffer*& slot = g_pictures[focused ? 1 : 0];
  if (!slot) {
    void* pixels = nullptr;
    size_t stride = 0;
    wlr_buffer* b = create_pixel_buffer(kit::kShadowSize, kit::kShadowSize, &pixels, &stride);
    if (!b) return nullptr;
    cairo_surface_t* surf = cairo_image_surface_create_for_data(static_cast<unsigned char*>(pixels), CAIRO_FORMAT_ARGB32, kit::kShadowSize,
                                                                kit::kShadowSize, static_cast<int>(stride));
    cairo_t* cr = cairo_create(surf);
    kit::draw_window_shadow(cr, focused);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    slot = b;  // this process keeps the producer's reference
  }
  return wlr_buffer_lock(slot);
}

void shadow_clear() {}

}  // namespace fleetwm
