#include "bar_config.hpp"

#include <gtest/gtest.h>

#include <fstream>

#include "test_util.hpp"

namespace fleetwm {
namespace {

using BarConfigTest = testutil::ScopedConfigHome;

// -- PowerMode <-> string conversions -------------------------------------

TEST(PowerMode, RoundTripsAllValues) {
  for (PowerMode mode : {PowerMode::Normal, PowerMode::Performance, PowerMode::BatterySaver}) {
    EXPECT_EQ(power_mode_from_string(power_mode_to_string(mode)), mode);
  }
}

TEST(PowerMode, UnknownStringFallsBackToNormal) {
  EXPECT_EQ(power_mode_from_string("bogus"), PowerMode::Normal);
  EXPECT_EQ(power_mode_from_string(""), PowerMode::Normal);
}

TEST(PowerMode, ProfilesDaemonNamesMatchPowerProfilesDaemon) {
  EXPECT_EQ(power_mode_to_profiles_daemon_name(PowerMode::Normal), "balanced");
  EXPECT_EQ(power_mode_to_profiles_daemon_name(PowerMode::Performance), "performance");
  EXPECT_EQ(power_mode_to_profiles_daemon_name(PowerMode::BatterySaver), "power-saver");
}

// -- load_bar_config() / save_bar_config() --------------------------------

TEST_F(BarConfigTest, LoadWithNoConfigFileReturnsDefaults) {
  BarConfig config = load_bar_config();
  EXPECT_TRUE(config.clock.show_seconds);
  EXPECT_FALSE(config.clock.show_date);
  EXPECT_EQ(config.workspace_colors.inactive_bg, "#3c3c3c");
  EXPECT_EQ(config.power_mode, PowerMode::Normal);
}

TEST_F(BarConfigTest, SaveThenLoadRoundTripsNestedTables) {
  BarConfig config;
  config.clock.show_seconds = false;
  config.clock.show_date = true;
  config.clock.show_year = false;
  config.clock.show_month = false;
  config.clock.show_day = false;
  config.workspace_colors.inactive_bg = "#111111";
  config.workspace_colors.inactive_fg = "#222222";
  config.workspace_colors.active_bg = "#333333";
  config.workspace_colors.active_fg = "#444444";
  config.workspace_colors.buttons_rounded = false;
  config.power_mode = PowerMode::Performance;

  save_bar_config(config);
  BarConfig loaded = load_bar_config();

  EXPECT_FALSE(loaded.clock.show_seconds);
  EXPECT_TRUE(loaded.clock.show_date);
  EXPECT_FALSE(loaded.clock.show_year);
  EXPECT_EQ(loaded.workspace_colors.inactive_bg, "#111111");
  EXPECT_EQ(loaded.workspace_colors.active_fg, "#444444");
  EXPECT_FALSE(loaded.workspace_colors.buttons_rounded);
  EXPECT_EQ(loaded.power_mode, PowerMode::Performance);
}

TEST_F(BarConfigTest, MissingNestedTableKeepsDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "bar.toml");
  out << "power_mode = \"battery_saver\"\n";
  out.close();

  BarConfig config = load_bar_config();
  EXPECT_EQ(config.power_mode, PowerMode::BatterySaver);
  EXPECT_TRUE(config.clock.show_seconds);
  EXPECT_EQ(config.workspace_colors.inactive_bg, "#3c3c3c");
}

TEST_F(BarConfigTest, PartiallyFilledNestedTableKeepsRemainingDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "bar.toml");
  out << "[clock]\nshow_seconds = false\n";
  out.close();

  BarConfig config = load_bar_config();
  EXPECT_FALSE(config.clock.show_seconds);
  EXPECT_FALSE(config.clock.show_date);  // still default
  EXPECT_TRUE(config.clock.show_year);   // still default
}

}  // namespace
}  // namespace fleetwm
