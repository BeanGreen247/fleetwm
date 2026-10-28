#include <gtest/gtest.h>

#include <vector>

#include "xembed_tray.hpp"

namespace fleetwm::bar {
namespace {

TEST(XembedTray, RecognisesXwaylandByCommName) {
  EXPECT_TRUE(is_xwayland_comm("Xwayland\n"));
  EXPECT_TRUE(is_xwayland_comm("Xwayland"));
  EXPECT_FALSE(is_xwayland_comm("Xorg\n"));
  EXPECT_FALSE(is_xwayland_comm("xwayland-satellite\n"));
}

TEST(XembedTray, MapsMouseButtons) {
  EXPECT_EQ(x_button_for(0x110), 1);
  EXPECT_EQ(x_button_for(0x112), 2);
  EXPECT_EQ(x_button_for(0x111), 3);
  EXPECT_EQ(x_button_for(0x113), 0);
}

TEST(XembedTray, ConvertsBgrxToOpaqueArgb) {
  // two pixels, one row, with a 4 byte row padding: B G R X
  const unsigned char src[8] = {10, 20, 30, 0, 40, 50, 60, 77};
  std::vector<unsigned char> dst(12, 0xee);
  bgrx_to_argb(src, 8, dst.data(), 12, 2, 1);
  EXPECT_EQ(dst[0], 10);
  EXPECT_EQ(dst[1], 20);
  EXPECT_EQ(dst[2], 30);
  EXPECT_EQ(dst[3], 255);  // the unused X byte becomes full alpha
  EXPECT_EQ(dst[7], 255);
  EXPECT_EQ(dst[8], 0xee);  // padding is left alone
}

TEST(XembedTray, KeepsTransparencyOfArgbIcons) {
  // a clear corner, a half transparent pixel, and one with a colour above its alpha (broken program) that gets clamped
  const unsigned char src[12] = {0, 0, 0, 0, 40, 20, 10, 128, 255, 90, 90, 100};
  std::vector<unsigned char> dst(12, 0xee);
  bgra_to_argb(src, 12, dst.data(), 12, 3, 1);
  EXPECT_EQ(dst[3], 0);    // fully transparent stays so
  EXPECT_EQ(dst[7], 128);  // alpha is kept, not forced to 255
  EXPECT_EQ(dst[4], 40);
  EXPECT_EQ(dst[8], 100);  // 255 > alpha 100: clamped
  EXPECT_EQ(dst[9], 90);
}

}  // namespace
}  // namespace fleetwm::bar
