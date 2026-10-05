// SVG rasterization via the vendored nanosvg (zlib license, see
// third_party/NANOSVG-LICENSE.txt). Kept in its own translation unit so the
// header-only implementation is compiled once.
#include <cstdlib>
#include <cstring>

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "third_party/nanosvg.h"
#include "third_party/nanosvgrast.h"

#include "image.hpp"

namespace fleetwm::kit {

Image load_svg(const std::string& path, int size) {
  Image img;
  NSVGimage* svg = nsvgParseFromFile(path.c_str(), "px", 96.0f);
  if (!svg) return img;
  if (svg->width <= 0 || svg->height <= 0) {
    nsvgDelete(svg);
    return img;
  }
  const float scale = static_cast<float>(size) / (svg->width > svg->height ? svg->width : svg->height);
  const int w = static_cast<int>(svg->width * scale + 0.5f);
  const int h = static_cast<int>(svg->height * scale + 0.5f);
  if (w > 0 && h > 0) {
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (rast) {
      img.rgba.resize(static_cast<size_t>(w) * h * 4);
      nsvgRasterize(rast, svg, 0, 0, scale, img.rgba.data(), w, h, w * 4);
      img.width = w;
      img.height = h;
      nsvgDeleteRasterizer(rast);
    }
  }
  nsvgDelete(svg);
  return img;
}

}  // namespace fleetwm::kit
