#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fleetwm::kit {

// Decoded image: tightly packed 8-bit RGBA, row-major, no padding.
struct Image {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> rgba;
  bool ok() const { return width > 0 && height > 0; }
};

// Decodes PNG, JPEG or WebP (detected from magic bytes, not the file
// extension). Returns an empty Image on any failure.
Image load_image(const std::string& path);

// Rasterizes an SVG file to a square-ish RGBA image whose larger side is
// `size` px (nanosvg: paths, fills, strokes, gradients; no filters/text).
Image load_svg(const std::string& path, int size);

// Cover-fit scale: scales `src` so it fully covers dst_w x dst_h, centered,
// cropping the overflow (same as GTK_CONTENT_FIT_COVER). Writes
// XRGB8888/ARGB8888 little-endian pixels (B,G,R,A byte order) into `dst`,
// which must hold dst_w * dst_h * 4 bytes.
void render_cover(const Image& src, int dst_w, int dst_h, uint8_t* dst);

}  // namespace fleetwm::kit
