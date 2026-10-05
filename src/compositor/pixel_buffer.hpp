#pragma once

#include <cstddef>
#include <cstdint>

struct wlr_buffer;

namespace fleetwm {

// A CPU-memory ARGB8888 (premultiplied) wlr_buffer, for things the compositor draws
// itself: titlebars and the fallback mouse cursor. `*pixels` points at the zeroed
// pixel memory to fill in (row stride is `*stride` bytes); the buffer owns it and
// frees it when the last reference is dropped. Returns nullptr on allocation failure.
// The caller owns one reference (wlr_buffer_drop() it once handed over).
wlr_buffer* create_pixel_buffer(int width, int height, void** pixels, size_t* stride);

}  // namespace fleetwm
