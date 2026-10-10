#include "output_config.hpp"

#include <gtest/gtest.h>

#include <fstream>

#include "test_util.hpp"

namespace fleetwm {
namespace {

using OutputConfigTest = testutil::ScopedConfigHome;

TEST_F(OutputConfigTest, NoFileMeansNoSettings) {
  EXPECT_TRUE(load_output_settings().empty());
}

TEST_F(OutputConfigTest, RoundTripsModeAndPosition) {
  OutputSettings in;
  in["DP-1"] = {2560, 1440, 144000, true, 0, 0};
  in["HDMI-A-1"] = {1920, 1080, 60000, true, 2560, 180};
  save_output_settings(in);
  const OutputSettings out = load_output_settings();
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out.at("DP-1").width, 2560);
  EXPECT_EQ(out.at("DP-1").refresh_mhz, 144000);
  EXPECT_TRUE(out.at("HDMI-A-1").has_pos);
  EXPECT_EQ(out.at("HDMI-A-1").x, 2560);
  EXPECT_EQ(out.at("HDMI-A-1").y, 180);
}

TEST_F(OutputConfigTest, PositionOnlyKeepsPreferredMode) {
  OutputSettings in;
  in["eDP-1"] = {0, 0, 0, true, 100, 50};
  save_output_settings(in);
  const OutputSettings out = load_output_settings();
  EXPECT_EQ(out.at("eDP-1").width, 0);
  EXPECT_TRUE(out.at("eDP-1").has_pos);
}

TEST_F(OutputConfigTest, RoundTripsDisplayPreferences) {
  save_display_settings({"DP-1", false});
  const DisplaySettings out = load_display_settings();
  EXPECT_EQ(out.primary_output, "DP-1");
  EXPECT_FALSE(out.taskbar_all_displays);
}

TEST_F(OutputConfigTest, CorruptFileIsIgnored) {
  save_output_settings({{"DP-1", {1920, 1080, 0, false, 0, 0}}});
  std::ofstream(output_config_path()) << "this is [not valid toml";
  EXPECT_TRUE(load_output_settings().empty());
}

}  // namespace
}  // namespace fleetwm
