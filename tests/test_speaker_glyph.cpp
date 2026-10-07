#include "speaker_glyph.hpp"

#include <cairo.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

namespace fleetwm {
namespace {

int speaker_lit_pixels(int waves, bool crossed = false, int* red = nullptr) {
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 32, 32);
  cairo_t* cr = cairo_create(surf);
  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_paint(cr);
  kit::draw_speaker_glyph(cr, 16, 16, 24, waves, 1, 1, 1, crossed);
  cairo_surface_flush(surf);
  const uint32_t* px = reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(surf));
  const int stride = cairo_image_surface_get_stride(surf) / 4;
  int n = 0, r = 0;
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 32; ++x) {
      const uint32_t v = px[y * stride + x];
      const int rr = (v >> 16) & 255, gg = (v >> 8) & 255, bb = v & 255;
      if (rr > 128 && gg > 128 && bb > 128) ++n;
      if (rr > gg + 80 && rr > bb + 80) ++r;
    }
  if (red) *red = r;
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return n;
}

TEST(SpeakerGlyph, WavesFollowTheVolume) {
  EXPECT_EQ(kit::speaker_waves(0), 0);
  EXPECT_EQ(kit::speaker_waves(1), 1);
  EXPECT_EQ(kit::speaker_waves(33), 1);
  EXPECT_EQ(kit::speaker_waves(34), 2);
  EXPECT_EQ(kit::speaker_waves(66), 2);
  EXPECT_EQ(kit::speaker_waves(67), 3);
  EXPECT_EQ(kit::speaker_waves(150), 3);
  EXPECT_EQ(kit::speaker_waves(-5), 0);
}

TEST(SpeakerGlyph, EveryExtraWaveDrawsMoreAndTheConeIsAlwaysThere) {
  const int none = speaker_lit_pixels(0), one = speaker_lit_pixels(1), two = speaker_lit_pixels(2), three = speaker_lit_pixels(3);
  EXPECT_GT(none, 20);
  EXPECT_GT(one, none);
  EXPECT_GT(two, one);
  EXPECT_GT(three, two);
}

TEST(SpeakerGlyph, CrossedAddsARedSlashAndDimsTheSpeaker) {
  int red_plain = 0, red_crossed = 0;
  const int plain = speaker_lit_pixels(2, false, &red_plain);
  const int crossed = speaker_lit_pixels(2, true, &red_crossed);
  EXPECT_EQ(red_plain, 0);
  EXPECT_GT(red_crossed, 0);
  EXPECT_LT(crossed, plain);
}

// The order and contents of the taskbar's right-hand side are decided in the bar's source; these checks
// keep the layout the owner asked for from drifting: volume as an icon only, after the keyboard layout and
// before the network icon, the clock last and no power button on the desktop taskbar.
std::string bar_source() {
  std::ifstream in(std::string(FLEETWM_SOURCE_DIR) + "/src/bar/main.cpp");
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

TEST(TaskbarLayout, VolumeIsAnIconAfterTheKeyboardLayoutAndBeforeTheNetworkIcon) {
  const std::string src = bar_source();
  const size_t group = src.find("double draw_status_group(");
  ASSERT_NE(group, std::string::npos);
  const size_t pill = src.find("draw_layout_pill(cr, m, rx, H / 2.0, lw);", group);
  const size_t vol = src.find("volume();  // after the keyboard layout", group);
  const size_t net = src.find("draw_net_glyph(cr, rx + kNetW / 2", group);
  ASSERT_NE(pill, std::string::npos);
  ASSERT_NE(vol, std::string::npos);
  ASSERT_NE(net, std::string::npos);
  EXPECT_LT(pill, vol);
  EXPECT_LT(vol, net);
  EXPECT_EQ(src.find("vol_value_text"), std::string::npos) << "no percentage text next to the speaker";
  EXPECT_NE(src.find("return vol_text + \"\\nClick to open the audio mixer\""), std::string::npos) << "the percentage stays in the tooltip";
}

TEST(TaskbarLayout, ClockIsTheLastItemAndTheDesktopTaskbarHasNoPowerButton) {
  const std::string src = bar_source();
  EXPECT_NE(src.find("const double clock_x = W - kMargin - clock_w, right_x = clock_x - 18 - right_w;"), std::string::npos);
  EXPECT_NE(src.find("if (taskbar) {\n      power_rect = {};\n      return rx - kRightGap;"), std::string::npos);
  EXPECT_NE(src.find("power_rect = {};  // no power button on the taskbar"), std::string::npos);
  EXPECT_EQ(src.find("draw_power_glyph(cr, W / 2.0, y + kBtn / 2"), std::string::npos);
}

}  // namespace
}  // namespace fleetwm
