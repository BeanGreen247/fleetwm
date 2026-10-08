#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "mouse_config.hpp"
#include "test_util.hpp"

using namespace fleetwm;

using MouseConfigTest = testutil::ScopedConfigHome;

TEST_F(MouseConfigTest, DefaultsToTheMiddleNotchWithPrecisionOn) {
  const MouseConfig c = load_mouse_config();
  EXPECT_EQ(c.speed, 6);
  EXPECT_TRUE(c.enhanced_precision);
  EXPECT_FALSE(c.swap_buttons);
  EXPECT_FALSE(c.natural_scroll);
}

TEST_F(MouseConfigTest, RoundTrips) {
  MouseConfig c;
  c.speed = 9;
  c.enhanced_precision = false;
  c.swap_buttons = true;
  c.natural_scroll = true;
  save_mouse_config(c);
  EXPECT_EQ(load_mouse_config(), c);
  c.speed = 1;
  c.swap_buttons = false;
  c.natural_scroll = false;
  c.enhanced_precision = true;
  save_mouse_config(c);
  EXPECT_EQ(load_mouse_config(), c);
}

TEST_F(MouseConfigTest, SpeedIsClampedToTheElevenNotchesWhenLoadedAndSaved) {
  write_config("mouse.toml", "speed = 99\n");
  EXPECT_EQ(load_mouse_config().speed, 11);
  write_config("mouse.toml", "speed = -5\n");
  EXPECT_EQ(load_mouse_config().speed, 1);
  MouseConfig c;
  c.speed = 400;
  save_mouse_config(c);
  EXPECT_EQ(load_mouse_config().speed, 11);
}

TEST_F(MouseConfigTest, ABrokenFileGivesTheDefaultsAndAnUnknownKeyIsIgnored) {
  write_config("mouse.toml", "speed = [\n");
  EXPECT_EQ(load_mouse_config(), MouseConfig{});
  write_config("mouse.toml", "speed = 3\nsomething_else = true\n");
  EXPECT_EQ(load_mouse_config().speed, 3);
  EXPECT_TRUE(load_mouse_config().enhanced_precision);
}

TEST_F(MouseConfigTest, ButtonAndScrollOptionsLoadFromTheFileAndIgnoreOtherTypes) {
  write_config("mouse.toml", "swap_buttons = true\nnatural_scroll = true\n");
  EXPECT_TRUE(load_mouse_config().swap_buttons);
  EXPECT_TRUE(load_mouse_config().natural_scroll);
  write_config("mouse.toml", "swap_buttons = \"yes\"\n");
  EXPECT_FALSE(load_mouse_config().swap_buttons);
}

TEST(MouseSpeed, NotchSixIsLibinputZeroAndTheEndsAreMinusOneAndPlusOne) {
  EXPECT_DOUBLE_EQ(libinput_speed_for_notch(6), 0.0);
  EXPECT_DOUBLE_EQ(libinput_speed_for_notch(1), -1.0);
  EXPECT_DOUBLE_EQ(libinput_speed_for_notch(11), 1.0);
  EXPECT_DOUBLE_EQ(libinput_speed_for_notch(0), -1.0);   // out of range clamps
  EXPECT_DOUBLE_EQ(libinput_speed_for_notch(50), 1.0);
}

TEST(MouseSpeed, EveryNotchIsFasterThanTheOneBeforeIt) {
  double last = -2.0;
  for (int n = kMouseSpeedMin; n <= kMouseSpeedMax; ++n) {
    const double s = libinput_speed_for_notch(n);
    EXPECT_GT(s, last) << n;
    last = s;
  }
}

TEST(MouseSpeed, EnhancedPrecisionIsTheAcceleratingProfile) {
  MouseConfig c;
  c.enhanced_precision = true;
  EXPECT_TRUE(wants_adaptive_profile(c));
  c.enhanced_precision = false;
  EXPECT_FALSE(wants_adaptive_profile(c));
}

namespace {
std::string mouse_read(const char* rel) {
  std::ifstream in(std::string(FLEETWM_SOURCE_DIR) + "/" + rel);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
}  // namespace

TEST(MouseSettings, TheCompositorAppliesItToEveryPointerAndWatchesTheFile) {
  const std::string server = mouse_read("src/compositor/server.cpp");
  EXPECT_NE(server.find("libinput_device_config_accel_set_speed("), std::string::npos);
  EXPECT_NE(server.find("libinput_device_config_accel_set_profile("), std::string::npos);
  EXPECT_NE(server.find("\"mouse.toml\""), std::string::npos) << "a saved change must reach the compositor live";
  EXPECT_NE(server.find("apply_mouse_config(device)"), std::string::npos) << "new pointers get the setting too";
}

TEST(MouseSettings, SettingsHasAMouseTabWithTheSliderTheToggleAndAPreview) {
  const std::string settings = mouse_read("apps/settings/main.cpp");
  EXPECT_NE(settings.find("\"Keyboard\", \"Mouse\""), std::string::npos);
  EXPECT_NE(settings.find("Enhance pointer precision"), std::string::npos);
  EXPECT_NE(settings.find("Pointer speed"), std::string::npos);
  EXPECT_NE(settings.find("save_mouse_config("), std::string::npos);
  EXPECT_NE(settings.find("kit::draw_cursor("), std::string::npos) << "the tab previews the pointer shapes";
}
