#include "pixel_buffer.hpp"

#include <drm_fourcc.h>

#include <cstdlib>

extern "C" {
#include <wlr/interfaces/wlr_buffer.h>
}

namespace fleetwm {

namespace {

struct PixelBuffer {
  wlr_buffer base;
  void* data;
  size_t stride;
};

void destroy(wlr_buffer* buffer) {
  PixelBuffer* pb = wl_container_of(buffer, pb, base);
  std::free(pb->data);
  delete pb;
}

bool begin_access(wlr_buffer* buffer, uint32_t flags, void** data, uint32_t* format, size_t* stride) {
  if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE) return false;  // read-only once handed to the scene
  PixelBuffer* pb = wl_container_of(buffer, pb, base);
  *data = pb->data;
  *format = DRM_FORMAT_ARGB8888;
  *stride = pb->stride;
  return true;
}

void end_access(wlr_buffer*) {}

const wlr_buffer_impl kImpl = {destroy, nullptr, nullptr, begin_access, end_access};

}  // namespace

wlr_buffer* create_pixel_buffer(int width, int height, void** pixels, size_t* stride) {
  if (width < 1 || height < 1) return nullptr;
  auto* pb = new PixelBuffer;
  pb->stride = static_cast<size_t>(width) * 4;
  pb->data = std::calloc(static_cast<size_t>(height), pb->stride);
  if (!pb->data) {
    delete pb;
    return nullptr;
  }
  wlr_buffer_init(&pb->base, &kImpl, width, height);
  *pixels = pb->data;
  *stride = pb->stride;
  return &pb->base;
}

}  // namespace fleetwm
