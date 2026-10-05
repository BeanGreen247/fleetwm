#include <gtest/gtest.h>

#include <algorithm>

#include "power_config.hpp"
#include "test_util.hpp"

using namespace fleetwm;

using PowerConfigTest = testutil::ScopedConfigHome;

TEST_F(PowerConfigTest, DefaultsWhenThereIsNoFile) {
  const PowerConfig c = load_power_config();
  EXPECT_EQ(c.ac, (PowerProfile{15, 0}));
  EXPECT_EQ(c.battery, (PowerProfile{5, 15}));
}

TEST_F(PowerConfigTest, RoundTrips) {
  PowerConfig c;
  c.ac = {30, 60};
  c.battery = {2, 10};
  save_power_config(c);
  EXPECT_EQ(load_power_config(), c);
}

TEST_F(PowerConfigTest, NeverIsZeroAndRoundTrips) {
  PowerConfig c;
  c.ac = {0, 0};
  save_power_config(c);
  EXPECT_EQ(load_power_config().ac, (PowerProfile{0, 0}));
}

TEST_F(PowerConfigTest, ProfilesAreIndependent) {
  write_config("power.toml", "[battery]\nsleep_minutes = 3\n");
  const PowerConfig c = load_power_config();
  EXPECT_EQ(c.battery.sleep_minutes, 3);
  EXPECT_EQ(c.battery.display_off_minutes, 5);  // untouched default
  EXPECT_EQ(c.ac, (PowerProfile{15, 0}));
}

TEST_F(PowerConfigTest, ValuesAreClamped) {
  write_config("power.toml", "[ac]\ndisplay_off_minutes = -5\nsleep_minutes = 99999\n");
  const PowerConfig c = load_power_config();
  EXPECT_EQ(c.ac.display_off_minutes, 0);
  EXPECT_EQ(c.ac.sleep_minutes, kMaxPowerMinutes);
}

TEST_F(PowerConfigTest, WrongTypesKeepTheDefaults) {
  write_config("power.toml", "[ac]\ndisplay_off_minutes = \"ten\"\nsleep_minutes = true\n");
  EXPECT_EQ(load_power_config().ac, (PowerProfile{15, 0}));
}

TEST_F(PowerConfigTest, BrokenFileGivesTheDefaultsInsteadOfFailing) {
  write_config("power.toml", "[ac\nthis is not toml");
  EXPECT_EQ(load_power_config(), PowerConfig{});
}

TEST_F(PowerConfigTest, SettingsArePerUser) {
  PowerConfig mine;
  mine.ac = {1, 2};
  save_power_config(mine);
  const auto other = dir_ / "other";
  std::filesystem::create_directories(other);
  ::setenv("XDG_CONFIG_HOME", other.c_str(), 1);
  EXPECT_EQ(load_power_config(), PowerConfig{});
  ::setenv("XDG_CONFIG_HOME", dir_.c_str(), 1);
  EXPECT_EQ(load_power_config().ac, (PowerProfile{1, 2}));
}

TEST(PowerChoices, StartWithNeverAndAreIncreasing) {
  const auto& c = power_timeout_choices();
  ASSERT_GE(c.size(), 5u);
  EXPECT_EQ(c.front(), 0);
  EXPECT_TRUE(std::is_sorted(c.begin(), c.end()));
}

TEST(PowerChoices, IncludeTheDefaults) {
  const auto& c = power_timeout_choices();
  for (int m : {0, 5, 15})
    EXPECT_NE(std::find(c.begin(), c.end(), m), c.end()) << m;
}

TEST(PowerLabels, ReadNaturally) {
  EXPECT_EQ(power_timeout_label(0), "Never");
  EXPECT_EQ(power_timeout_label(1), "1 minute");
  EXPECT_EQ(power_timeout_label(15), "15 minutes");
  EXPECT_EQ(power_timeout_label(60), "1 hour");
  EXPECT_EQ(power_timeout_label(120), "2 hours");
  EXPECT_EQ(power_timeout_label(90), "90 minutes");
  EXPECT_EQ(power_timeout_label(-3), "Never");
}

TEST(NearestChoice, ExactValuesMapToThemselves) {
  const auto& c = power_timeout_choices();
  for (size_t i = 0; i < c.size(); ++i) EXPECT_EQ(nearest_power_timeout_index(c[i]), static_cast<int>(i));
}

TEST(NearestChoice, OddValuesSnapToTheClosestChoice) {
  const auto& c = power_timeout_choices();
  EXPECT_EQ(c[static_cast<size_t>(nearest_power_timeout_index(7))], 5);
  EXPECT_EQ(c[static_cast<size_t>(nearest_power_timeout_index(8))], 10);
  EXPECT_EQ(c[static_cast<size_t>(nearest_power_timeout_index(1000))], 120);
}

TEST(NearestChoice, SmallPositiveValuesNeverBecomeNever) {
  EXPECT_NE(nearest_power_timeout_index(1), 0);
}

TEST(IdleActions, NothingHappensBeforeTheFirstDeadline) {
  const IdleActions a = idle_actions({5, 15}, 100);
  EXPECT_FALSE(a.blank_display);
  EXPECT_FALSE(a.suspend);
  EXPECT_EQ(a.next_check_seconds, 5 * 60 - 100);
}

TEST(IdleActions, BlankAtTheDisplayTimeThenWaitForSleep) {
  const IdleActions a = idle_actions({5, 15}, 5 * 60);
  EXPECT_TRUE(a.blank_display);
  EXPECT_FALSE(a.suspend);
  EXPECT_EQ(a.next_check_seconds, 10 * 60);
}

TEST(IdleActions, SuspendAtTheSleepTime) {
  const IdleActions a = idle_actions({5, 15}, 15 * 60);
  EXPECT_TRUE(a.blank_display);
  EXPECT_TRUE(a.suspend);
  EXPECT_EQ(a.next_check_seconds, -1);  // nothing further to wait for
}

TEST(IdleActions, NeverMeansNoAction) {
  const IdleActions a = idle_actions({0, 0}, 100000);
  EXPECT_FALSE(a.blank_display);
  EXPECT_FALSE(a.suspend);
  EXPECT_EQ(a.next_check_seconds, -1);
}

TEST(IdleActions, OnlyTheEnabledTimerCounts) {
  EXPECT_TRUE(idle_actions({10, 0}, 600).blank_display);
  EXPECT_FALSE(idle_actions({10, 0}, 600).suspend);
  EXPECT_TRUE(idle_actions({0, 10}, 600).suspend);
  EXPECT_FALSE(idle_actions({0, 10}, 600).blank_display);
}

TEST(IdleActions, SleepCanComeBeforeTheDisplayTimeout) {
  const IdleActions a = idle_actions({30, 5}, 5 * 60);
  EXPECT_TRUE(a.suspend);
  EXPECT_FALSE(a.blank_display);
  EXPECT_EQ(a.next_check_seconds, 25 * 60);
}

TEST(IdleActions, NextCheckIsTheNearestFutureDeadline) {
  EXPECT_EQ(idle_actions({10, 5}, 0).next_check_seconds, 5 * 60);
  EXPECT_EQ(idle_actions({10, 5}, 120).next_check_seconds, 3 * 60);
}

TEST(IdleActions, ZeroIdleIsNeverAnAction) {
  const IdleActions a = idle_actions({1, 1}, 0);
  EXPECT_FALSE(a.blank_display);
  EXPECT_FALSE(a.suspend);
}
