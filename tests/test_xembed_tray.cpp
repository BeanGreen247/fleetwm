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

}  // namespace
}  // namespace fleetwm::bar
