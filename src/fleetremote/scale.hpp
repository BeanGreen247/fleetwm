#pragma once

// Render resolution: the size a viewer is sent when it is smaller than the screen. The picture is shrunk with a box
// filter (every output pixel is the average of the screen pixels it covers) and never enlarged.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fleetremote {

// The output size for a screen of `w`x`h` and a limit of `tw`x`th` (0 = no limit): the largest size inside the limit
// with the screen's proportions, even numbers, never bigger than the screen.
inline void fit_size(int w, int h, int tw, int th, int* ow, int* oh) {
  if (tw <= 0 || th <= 0 || (tw >= w && th >= h)) {
    *ow = w;
    *oh = h;
    return;
  }
  const double s = std::min(static_cast<double>(tw) / w, static_cast<double>(th) / h);
  *ow = std::max(16, static_cast<int>(std::lround(w * s)) & ~1);
  *oh = std::max(16, static_cast<int>(std::lround(h * s)) & ~1);
}

// Fills `dst` (dw x dh pixels, row-major) with the part of the scaled picture that starts at (dx0, dy0): the screen
// `src` (sw x sh) shrunk to ow x oh.
inline void box_scale(const uint32_t* src, int sw, int sh, int ow, int oh, int dx0, int dy0, int dw, int dh, uint32_t* dst) {
  for (int j = 0; j < dh; ++j) {
    const int dy = dy0 + j;
    const int sy0 = static_cast<int>(static_cast<int64_t>(dy) * sh / oh);
    const int sy1 = std::max(sy0 + 1, static_cast<int>(static_cast<int64_t>(dy + 1) * sh / oh));
    for (int i = 0; i < dw; ++i) {
      const int dx = dx0 + i;
      const int sx0 = static_cast<int>(static_cast<int64_t>(dx) * sw / ow);
      const int sx1 = std::max(sx0 + 1, static_cast<int>(static_cast<int64_t>(dx + 1) * sw / ow));
      uint32_t r = 0, g = 0, b = 0;
      for (int y = sy0; y < sy1; ++y) {
        const uint32_t* row = src + static_cast<size_t>(y) * sw;
        for (int x = sx0; x < sx1; ++x) {
          const uint32_t v = row[x];
          r += (v >> 16) & 0xFF;
          g += (v >> 8) & 0xFF;
          b += v & 0xFF;
        }
      }
      const uint32_t n = static_cast<uint32_t>((sy1 - sy0) * (sx1 - sx0));
      dst[static_cast<size_t>(j) * dw + i] = ((r / n) << 16) | ((g / n) << 8) | (b / n);
    }
  }
}

}  // namespace fleetremote
