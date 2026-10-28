#include "tuxthrottle.hpp"

#include <gtest/gtest.h>

namespace fleetwm {
namespace {

// `tuxthrottlectl status --json` on a Dell G15 5515 (the "backends" list left out).
const char* kStatus = R"JSON({"battery_charge_limit":{"ac_online":true,"capacity":100,"current":null,"dell_libsmbios_possible":true,"method":"libsmbios","supported":true},"cpu":{"freq_ghz":4.12,"power_w":6.74015773199612,"temp_c":52.625},"dgpu":{"awake":true,"clock_mhz":210,"power_w":6.958,"temp_c":50,"util_pct":0},"fans":[{"boost":0,"index":1,"label":"CPU Fan","max":4700,"rpm":0},{"boost":0,"index":2,"label":"GPU Fan","max":4400,"rpm":0}],"game_mode":false,"gamemode":{"active":false,"clients":0,"installed":true},"gpu_mode":null,"igpu":{"clock_mhz":400,"temp_c":51},"model":"Dell G15 5515","nvidia_power_limit":{"current":null,"default":80,"max":90,"min":1,"supported":false},"platform_profile":"balanced","tdp":null})JSON";

TEST(TuxThrottle, ParsesTheRealStatusOutput) {
  const TuxThrottleStatus st = parse_tuxthrottle_status(kStatus);
  ASSERT_TRUE(st.ok);
  EXPECT_EQ(st.model, "Dell G15 5515");
  EXPECT_EQ(st.platform_profile, "balanced");
  EXPECT_DOUBLE_EQ(st.cpu_temp_c, 52.625);
  EXPECT_NEAR(st.cpu_power_w, 6.74, 0.01);
  EXPECT_TRUE(st.dgpu_present);
  EXPECT_TRUE(st.dgpu_awake);
  EXPECT_DOUBLE_EQ(st.dgpu_temp_c, 50);
  EXPECT_FALSE(st.game_mode);
  EXPECT_TRUE(st.gamemoded_installed);
  EXPECT_EQ(st.gamemoded_clients, 0);
}

TEST(TuxThrottle, GameModeOnIsRead) {
  const TuxThrottleStatus st = parse_tuxthrottle_status(R"({"platform_profile":"performance","game_mode":true,"gamemode":{"installed":true,"clients":2}})");
  ASSERT_TRUE(st.ok);
  EXPECT_TRUE(st.game_mode);
  EXPECT_EQ(st.gamemoded_clients, 2);
  EXPECT_FALSE(st.dgpu_present);
}

TEST(TuxThrottle, GarbageAndEmptyAreNotOk) {
  EXPECT_FALSE(parse_tuxthrottle_status("").ok);
  EXPECT_FALSE(parse_tuxthrottle_status("tuxthrottlectl: command not found").ok);
  EXPECT_FALSE(parse_tuxthrottle_status("{\"cpu\": {").ok);
  EXPECT_FALSE(parse_tuxthrottle_status("[1,2]").ok);
  EXPECT_FALSE(parse_tuxthrottle_status("{}").ok);
}

TEST(TuxThrottle, StringsWithEscapesAndNullsDoNotBreakIt) {
  const TuxThrottleStatus st = parse_tuxthrottle_status(R"({"model":"A \"B\"","platform_profile":"quiet","tdp":null,"cpu":{"temp_c":40}})");
  ASSERT_TRUE(st.ok);
  EXPECT_EQ(st.model, "A \"B\"");
  EXPECT_DOUBLE_EQ(st.cpu_temp_c, 40);
}

}  // namespace
}  // namespace fleetwm

TEST(EverydayPowerProfile, KeepsWhatWasInForceBeforeTheGame) {
  EXPECT_EQ(fleetwm::everyday_power_profile("balanced", "", "power-saver"), "balanced");
  EXPECT_EQ(fleetwm::everyday_power_profile("power-saver", "balanced", "balanced"), "power-saver");
}

TEST(EverydayPowerProfile, PerformanceSeenAtGameStartIsNotTheEverydayOne) {
  // a game tool raised it before the compositor looked: go back to what was remembered, else to the Settings choice
  EXPECT_EQ(fleetwm::everyday_power_profile("performance", "balanced", "power-saver"), "balanced");
  EXPECT_EQ(fleetwm::everyday_power_profile("performance", "", "power-saver"), "power-saver");
  EXPECT_EQ(fleetwm::everyday_power_profile("performance", "performance", "balanced"), "balanced");
}

TEST(EverydayPowerProfile, FallsBackToBalancedOrPerformance) {
  EXPECT_EQ(fleetwm::everyday_power_profile("", "", ""), "balanced");
  EXPECT_EQ(fleetwm::everyday_power_profile("junk", "", "junk"), "balanced");
  EXPECT_EQ(fleetwm::everyday_power_profile("performance", "", "performance"), "performance");  // the user really runs performance
}
