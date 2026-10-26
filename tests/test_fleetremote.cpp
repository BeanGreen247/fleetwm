#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <vector>

#include "scale.hpp"
#include "shadow.hpp"
#include "throttle.hpp"

using namespace fleetremote;

namespace {

// A test screen whose every row is different, so a moved row can be told from a repeated one.
struct RemoteTestFrame {
  int w, h;
  std::vector<uint32_t> px;
  RemoteTestFrame(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * h_) {
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) px[static_cast<size_t>(y) * w + x] = static_cast<uint32_t>((y * 2654435761u) ^ (x * 40503u)) & 0xFFFFFF;
  }
};

Tile remote_test_tile(const RemoteTestFrame& f, int tx, int ty) {
  Tile t;
  t.x = tx * kTile;
  t.y = ty * kTile;
  t.w = std::min(kTile, f.w - t.x);
  t.h = std::min(kTile, f.h - t.y);
  t.px.resize(static_cast<size_t>(t.w) * t.h);
  for (int row = 0; row < t.h; ++row)
    for (int x = 0; x < t.w; ++x) t.px[static_cast<size_t>(row) * t.w + x] = f.px[static_cast<size_t>(t.y + row) * f.w + t.x + x];
  return t;
}

TileBatch remote_test_tiles(const RemoteTestFrame& f) {
  TileBatch b;
  for (int ty = 0; ty * kTile < f.h; ++ty)
    for (int tx = 0; tx * kTile < f.w; ++tx) b.v.push_back(remote_test_tile(f, tx, ty));
  b.n = b.v.size();
  return b;
}

void remote_test_send_all(Shadow& s, const RemoteTestFrame& f) {
  TileBatch b = remote_test_tiles(f);
  for (size_t i = 0; i < b.n; ++i) s.commit(b.v[i]);
}

}  // namespace

TEST(RemoteShadow, TilesTheViewerAlreadyHasAreDropped) {
  const RemoteTestFrame f(192, 128);
  Shadow s;
  s.reset(f.w, f.h);
  TileBatch b = remote_test_tiles(f);
  EXPECT_EQ(s.drop_unchanged(b), b.n) << "nothing was sent yet, so every tile is news";
  remote_test_send_all(s, f);
  b = remote_test_tiles(f);
  EXPECT_EQ(s.drop_unchanged(b), 0u);
  b.v[2].px[5] ^= 0x10;  // one pixel changes
  EXPECT_EQ(s.drop_unchanged(b), 1u);
}

TEST(RemoteShadow, FindsAScrollAndAppliesIt) {
  const RemoteTestFrame old_frame(256, 512);
  Shadow s;
  s.reset(old_frame.w, old_frame.h);
  remote_test_send_all(s, old_frame);

  // The content moves up by 13 rows; 13 new rows appear at the bottom.
  RemoteTestFrame now(256, 512);
  const int step = 13;
  for (int y = 0; y < now.h; ++y)
    for (int x = 0; x < now.w; ++x)
      now.px[static_cast<size_t>(y) * now.w + x] = y + step < now.h ? old_frame.px[static_cast<size_t>(y + step) * now.w + x] : 0x00ABCDEFu + y * 7 + x;
  TileBatch b = remote_test_tiles(now);
  size_t kept = s.drop_unchanged(b);
  ASSERT_EQ(kept, b.n);
  ScrollMove mv;
  ASSERT_TRUE(s.detect_scroll(b, kept, &mv));
  EXPECT_EQ(mv.dy, -step);
  EXPECT_GE(mv.h, 480);
  EXPECT_EQ(mv.x, 0);
  EXPECT_EQ(mv.w, 256);
  s.apply(mv);
  kept = s.drop_unchanged(b);
  EXPECT_LT(kept, b.n / 2) << "after the move only the tiles with new rows still differ";
}

TEST(RemoteShadow, UnrelatedPicturesAreNotAScroll) {
  const RemoteTestFrame a(256, 512);
  Shadow s;
  s.reset(a.w, a.h);
  remote_test_send_all(s, a);
  RemoteTestFrame b(256, 512);
  for (auto& p : b.px) p = (p * 1103515245u + 12345u) & 0xFFFFFF;
  TileBatch tiles = remote_test_tiles(b);
  const size_t kept = s.drop_unchanged(tiles);
  ScrollMove mv;
  EXPECT_FALSE(s.detect_scroll(tiles, kept, &mv));
}

TEST(RemoteShadow, NoScrollWhenTheViewerLostItsPicture) {
  const RemoteTestFrame a(256, 512);
  Shadow s;
  s.reset(a.w, a.h);
  remote_test_send_all(s, a);
  s.forget();
  TileBatch tiles = remote_test_tiles(a);
  ScrollMove mv;
  EXPECT_FALSE(s.detect_scroll(tiles, tiles.n, &mv));
}

TEST(RemoteQuality, QuantizingKeepsTheMidpoint) {
  uint32_t px[2] = {0xFFFFFF, 0x000000};
  quantize_pixels(px, 2, 3);
  EXPECT_EQ(px[0], 0xFCFCFCu | 0x040404u);
  EXPECT_EQ(px[1], 0x040404u);
  uint32_t same[1] = {0x123456};
  quantize_pixels(same, 1, 0);
  EXPECT_EQ(same[0], 0x123456u);
}

TEST(RemoteQuality, LowerQualityDropsMoreColourDetail) {
  EXPECT_EQ(quality_quant_bits(100), 0);
  EXPECT_EQ(quality_quant_bits(60), 2);
  EXPECT_EQ(quality_quant_bits(1), 4);
}

TEST(RemoteThrottle, TheFrameRateSetsTheShortestInterval) {
  EXPECT_EQ(fps_interval_us(24), 41666);
  EXPECT_EQ(fps_interval_us(60), 16666);
  EXPECT_EQ(fps_interval_us(0), 1000000);  // a nonsense value is clamped, not a division by zero
  Tuning tuning;
  tuning.fps = 24;
  Throttle t(tuning);
  const auto start = Clock::now();
  t.sent(1000, std::chrono::milliseconds(1), start);
  EXPECT_GE(t.next_allowed(), start + std::chrono::microseconds(41000));
  EXPECT_LE(t.next_allowed(), start + std::chrono::microseconds(42000));
}

TEST(RemoteThrottle, AQualityChangeDoesNotChangeTheFrameRate) {
  Tuning tuning;
  tuning.fps = 30;
  Throttle t(tuning);
  const auto start = Clock::now();
  tuning.quality = 10;
  t.sent(1000, std::chrono::milliseconds(1), start);
  const auto low = t.next_allowed();
  tuning.quality = 100;
  EXPECT_EQ(low, t.next_allowed());
}

TEST(RemoteThrottle, ALimitBelowTheFloorIsRaisedToIt) {
  Tuning tuning;
  tuning.fps = 24;
  tuning.quality = 40;
  tuning.max_kbps = 32;
  Throttle t(tuning);
  t.set_frame(1920, 1080);
  const double floor_kb = fleetwm::remote_min_kbps(40, 24, 1920, 1080);
  EXPECT_GT(floor_kb, 32);
  EXPECT_NEAR(t.budget(), floor_kb * 1024.0, 1);
  tuning.max_kbps = 100000;  // a generous limit is left alone
  EXPECT_NEAR(t.budget(), 100000 * 1024.0, 1);
}

TEST(RemoteThrottle, ABandwidthLimitSpacesTheUpdates) {
  Tuning tuning;
  tuning.max_kbps = 100;  // 100 KB/s
  tuning.fps = 1;         // a floor far below the limit
  Throttle t(tuning);
  t.set_frame(100, 100);
  EXPECT_NEAR(t.budget(), 100 * 1024.0, 1);
  const auto start = Clock::now();
  t.sent(102400, std::chrono::milliseconds(1), start);  // one second's worth of bytes
  EXPECT_GE(t.next_allowed(), start + std::chrono::milliseconds(990));
}

TEST(RemoteThrottle, NoLimitMeansOnlyTheFrameInterval) {
  Tuning tuning;
  tuning.quality = 90;
  Throttle t(tuning);
  EXPECT_EQ(t.budget(), 0);
  const auto start = Clock::now();
  t.sent(1000, std::chrono::milliseconds(1), start);
  EXPECT_LE(t.next_allowed(), start + std::chrono::microseconds(fps_interval_us(30) + 1000));
}

TEST(RemoteThrottle, ASlowLinkIsLearnedFromTheSends) {
  Tuning tuning;  // no configured limit
  Throttle t(tuning);
  t.sent(100000, std::chrono::seconds(1), Clock::now());  // 100 KB took a second: the link carries about 100 KB/s
  EXPECT_GT(t.budget(), 50000);
  EXPECT_LT(t.budget(), 100000);
}

TEST(RemoteScale, FitsInsideTheLimitKeepingProportionsAndNeverEnlarges) {
  int w, h;
  fit_size(1920, 1080, 1280, 720, &w, &h);
  EXPECT_EQ(w, 1280);
  EXPECT_EQ(h, 720);
  fit_size(1920, 1080, 1024, 768, &w, &h);  // a 4:3 box: the width decides
  EXPECT_EQ(w, 1024);
  EXPECT_EQ(h, 576);
  fit_size(1280, 720, 1920, 1080, &w, &h);  // bigger than the screen: unchanged
  EXPECT_EQ(w, 1280);
  EXPECT_EQ(h, 720);
  fit_size(1280, 720, 0, 0, &w, &h);
  EXPECT_EQ(w, 1280);
}

TEST(RemoteScale, BoxFilterAveragesTheCoveredPixels) {
  // A 4x4 screen shrunk to 2x2: each output pixel is the mean of a 2x2 block.
  std::vector<uint32_t> src(16);
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x) src[static_cast<size_t>(y) * 4 + x] = (x < 2 ? 0x000000u : 0xFF0000u) | (y < 2 ? 0x00u : 0x80u);
  std::vector<uint32_t> dst(4);
  box_scale(src.data(), 4, 4, 2, 2, 0, 0, 2, 2, dst.data());
  EXPECT_EQ(dst[0], 0x000000u);
  EXPECT_EQ(dst[1], 0xFF0000u);
  EXPECT_EQ(dst[2], 0x000080u);
  EXPECT_EQ(dst[3], 0xFF0080u);
  // A part of the output gives the same pixels as the whole.
  uint32_t one;
  box_scale(src.data(), 4, 4, 2, 2, 1, 1, 1, 1, &one);
  EXPECT_EQ(one, 0xFF0080u);
}

TEST(RemoteQuality, DepthLimitsTheColours) {
  uint32_t px[1] = {0xFFFFFF};
  quantize_to_depth(px, 1, 8);
  EXPECT_EQ(px[0], 0xE0E0C0u | 0x101020u);
  uint32_t p16[1] = {0x123456};
  quantize_to_depth(p16, 1, 16);
  EXPECT_EQ(p16[0] & 0x070307u, 0x040204u);  // only the midpoint is left in the dropped bits
  uint32_t full[1] = {0x123457};
  quantize_to_depth(full, 1, 24);
  EXPECT_EQ(full[0], 0x123457u);
}
