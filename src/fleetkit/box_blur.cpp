#include "box_blur.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fleetwm::kit {

namespace {

// sum / div for 0 <= sum <= 255 * div as (sum * m) >> 16: a 16-bit multiply-high, which the vector units have; checked against the division here.
struct Mul16 {
  uint32_t div, m;
  bool ok = true;
  explicit Mul16(int d) : div(static_cast<uint32_t>(d)), m((65536u + static_cast<uint32_t>(d) - 1) / static_cast<uint32_t>(d)) {
    if (m > 65535) ok = false;
    for (uint32_t s = 0; ok && s <= 255u * div; ++s)
      if (((s * m) >> 16) != s / div) ok = false;
  }
};

// One vertical pass: out[y] = average of in[y-r .. y+r] (edge rows repeated) for `lines` rows of `row` bytes, each byte column on its own. A running
// sum per column walks down the rows, so all the work is along rows, where the compiler can use vector instructions.
template <class Sum, class Div>
void vpass(const uint8_t* in, size_t in_stride, uint8_t* out, size_t out_stride, size_t row, int lines, int r, std::vector<Sum>& colv, const Div& div) {
  Sum* __restrict col = colv.data();  // restrict: the byte stores to the picture must not be assumed to hit the sums
  std::fill(col, col + row, Sum(0));
  for (int y = -r; y <= r; ++y) {
    const uint8_t* __restrict t = in + static_cast<size_t>(std::clamp(y, 0, lines - 1)) * in_stride;
    for (size_t i = 0; i < row; ++i) col[i] = static_cast<Sum>(col[i] + t[i]);
  }
  for (int y = 0; y < lines; ++y) {
    uint8_t* __restrict d = out + static_cast<size_t>(y) * out_stride;
    for (size_t i = 0; i < row; ++i) d[i] = div(col[i]);
    const uint8_t* __restrict add = in + static_cast<size_t>(std::min(y + r + 1, lines - 1)) * in_stride;
    const uint8_t* __restrict sub = in + static_cast<size_t>(std::max(y - r, 0)) * in_stride;
    for (size_t i = 0; i < row; ++i) col[i] = static_cast<Sum>(col[i] + add[i] - sub[i]);
  }
}

struct MulDiv16 {
  uint32_t m;
  uint8_t operator()(uint16_t s) const { return static_cast<uint8_t>((static_cast<uint32_t>(s) * m) >> 16); }
};
struct PlainDiv32 {
  uint32_t div;
  uint8_t operator()(uint32_t s) const { return static_cast<uint8_t>(s / div); }
};

// out (h rows of w pixels) from in (w rows of h pixels): out[x][y] = in[y][x] for pixels of `channels` bytes.
void transpose(const uint8_t* in, size_t in_stride, uint8_t* out, size_t out_stride, int w, int h, int channels) {
  constexpr int B = 16;  // tiles, so neither side jumps through memory
  for (int y0 = 0; y0 < h; y0 += B)
    for (int x0 = 0; x0 < w; x0 += B) {
      const int y1 = std::min(y0 + B, h), x1 = std::min(x0 + B, w);
      if (channels == 4) {
        for (int y = y0; y < y1; ++y)
          for (int x = x0; x < x1; ++x)
            *reinterpret_cast<uint32_t*>(out + static_cast<size_t>(x) * out_stride + static_cast<size_t>(y) * 4) =
                *reinterpret_cast<const uint32_t*>(in + static_cast<size_t>(y) * in_stride + static_cast<size_t>(x) * 4);
      } else {
        for (int y = y0; y < y1; ++y)
          for (int x = x0; x < x1; ++x)
            for (int c = 0; c < channels; ++c)
              out[static_cast<size_t>(x) * out_stride + static_cast<size_t>(y) * channels + c] = in[static_cast<size_t>(y) * in_stride + static_cast<size_t>(x) * channels + c];
      }
    }
}

}  // namespace

void box_blur_u8(unsigned char* p, int w, int h, int stride, int channels, int r, int passes) {
  if (w < 1 || h < 1 || r < 1 || passes < 1) return;
  const Mul16 m(2 * r + 1);
  const MulDiv16 fast{m.m};
  const PlainDiv32 plain{static_cast<uint32_t>(2 * r + 1)};
  static thread_local std::vector<uint8_t> t1, t2;
  static thread_local std::vector<uint16_t> col16;
  static thread_local std::vector<uint32_t> col32;
  const size_t wrow = static_cast<size_t>(w) * channels, hrow = static_cast<size_t>(h) * channels;
  t1.resize(wrow * h);
  t2.resize(wrow * h);
  const size_t longest = std::max(wrow, hrow);
  if (m.ok) col16.resize(longest);
  else col32.resize(longest);
  auto vertical = [&](const uint8_t* in, size_t in_stride, uint8_t* out, size_t out_stride, size_t row, int lines) {
    if (m.ok) vpass<uint16_t>(in, in_stride, out, out_stride, row, lines, r, col16, fast);
    else vpass<uint32_t>(in, in_stride, out, out_stride, row, lines, r, col32, plain);
  };
  for (int pass = 0; pass < passes; ++pass) {
    // horizontal = vertical on the transposed picture
    transpose(p, static_cast<size_t>(stride), t1.data(), hrow, w, h, channels);  // t1: w rows of h pixels
    vertical(t1.data(), hrow, t2.data(), hrow, hrow, w);
    transpose(t2.data(), hrow, t1.data(), wrow, h, w, channels);  // back: h rows of w pixels
    vertical(t1.data(), wrow, p, static_cast<size_t>(stride), wrow, h);
  }
}

}  // namespace fleetwm::kit
