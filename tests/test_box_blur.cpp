// kit::box_blur_u8 must give exactly the bytes the old per-file blurs gave (a copy of the loop is kept here as the reference).
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "box_blur.hpp"

namespace {

void bb_reference(unsigned char* p, int w, int h, int stride, int channels, int r, int passes) {
  std::vector<unsigned char> tmp(static_cast<size_t>(stride) * h);
  const int div = 2 * r + 1;
  for (int pass = 0; pass < passes; ++pass) {
    for (int y = 0; y < h; ++y) {
      const unsigned char* s = p + static_cast<size_t>(y) * stride;
      unsigned char* d = &tmp[static_cast<size_t>(y) * stride];
      for (int c = 0; c < channels; ++c) {
        int sum = 0;
        for (int x = -r; x <= r; ++x) sum += s[std::clamp(x, 0, w - 1) * channels + c];
        for (int x = 0; x < w; ++x) {
          d[x * channels + c] = static_cast<unsigned char>(sum / div);
          sum += s[std::min(x + r + 1, w - 1) * channels + c] - s[std::max(x - r, 0) * channels + c];
        }
      }
    }
    for (int x = 0; x < w; ++x)
      for (int c = 0; c < channels; ++c) {
        int sum = 0;
        for (int y = -r; y <= r; ++y) sum += tmp[static_cast<size_t>(std::clamp(y, 0, h - 1)) * stride + x * channels + c];
        for (int y = 0; y < h; ++y) {
          p[static_cast<size_t>(y) * stride + x * channels + c] = static_cast<unsigned char>(sum / div);
          sum += tmp[static_cast<size_t>(std::min(y + r + 1, h - 1)) * stride + x * channels + c] -
                 tmp[static_cast<size_t>(std::max(y - r, 0)) * stride + x * channels + c];
        }
      }
  }
}

}  // namespace

TEST(BoxBlur, SameBytesAsTheOldLoopForEverySizeRadiusAndPassCount) {
  std::srand(7);
  int cases = 0;
  for (const int channels : {1, 4})
    for (const int r : {1, 3, 5, 6, 8})
      for (const int passes : {1, 2, 3})
        for (const auto [w, h] : {std::pair{1, 1}, std::pair{2, 5}, std::pair{7, 3}, std::pair{20, 17}, std::pair{181, 24}, std::pair{33, 95}}) {
          const int stride = w * channels + (cases % 3) * 4;  // padded rows too
          std::vector<unsigned char> a(static_cast<size_t>(stride) * h);
          for (auto& v : a) v = static_cast<unsigned char>(std::rand() % 256);
          std::vector<unsigned char> b = a;
          bb_reference(a.data(), w, h, stride, channels, r, passes);
          fleetwm::kit::box_blur_u8(b.data(), w, h, stride, channels, r, passes);
          for (int y = 0; y < h; ++y)
            for (int i = 0; i < w * channels; ++i)
              ASSERT_EQ(a[static_cast<size_t>(y) * stride + i], b[static_cast<size_t>(y) * stride + i])
                  << channels << " ch, r " << r << ", " << passes << " passes, " << w << "x" << h << " at row " << y << " byte " << i;
          ++cases;
        }
  EXPECT_EQ(cases, 2 * 5 * 3 * 6);
}

TEST(BoxBlur, AFlatPictureStaysFlat) {
  std::vector<unsigned char> a(40 * 30 * 4, 200);
  fleetwm::kit::box_blur_u8(a.data(), 40, 30, 160, 4, 3, 3);
  for (unsigned char v : a) EXPECT_EQ(v, 200);
}
