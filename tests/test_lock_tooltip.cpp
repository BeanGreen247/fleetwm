#include <gtest/gtest.h>

#include "lock_tooltip.hpp"

using namespace fleetwm;

TEST(LockTooltip, OffShowsTheRealTimersAndNoHolders) {
  const LockTooltip t = describe_lock_state(false, {}, PowerConfig{}, false);
  EXPECT_EQ(t.title, "Keep awake is off");
  EXPECT_NE(t.body.find("Screen off: after 15 minutes"), std::string::npos);
  EXPECT_NE(t.body.find("Sleep: never"), std::string::npos);
  EXPECT_EQ(t.body.find("Kept awake by"), std::string::npos);
}

TEST(LockTooltip, BatteryProfileIsUsedOnBattery) {
  const LockTooltip t = describe_lock_state(false, {}, PowerConfig{}, true);
  EXPECT_NE(t.body.find("Screen off: after 5 minutes"), std::string::npos);
  EXPECT_NE(t.body.find("Sleep: after 15 minutes"), std::string::npos);
}

TEST(LockTooltip, OnPausesTimersAndListsYou) {
  const LockTooltip t = describe_lock_state(true, {}, PowerConfig{}, false);
  EXPECT_EQ(t.title, "Keep awake is on");
  EXPECT_NE(t.body.find("Screen off: paused"), std::string::npos);
  EXPECT_NE(t.body.find("Sleep: paused"), std::string::npos);
  EXPECT_NE(t.body.find("- you (this padlock)"), std::string::npos);
}

TEST(LockTooltip, ProgramsBlockingAreListedEvenWhenOff) {
  const LockTooltip t = describe_lock_state(false, {{"mpv", "Playing video"}, {"firefox", ""}}, PowerConfig{}, false);
  EXPECT_EQ(t.title, "Keep awake is off");
  EXPECT_NE(t.body.find("- mpv - Playing video"), std::string::npos);
  EXPECT_NE(t.body.find("- firefox\n"), std::string::npos);
  EXPECT_NE(t.body.find("Screen off: paused"), std::string::npos);
}

TEST(LockTooltip, UniqueHoldersKeepsTheFirstIgnoringCase) {
  const auto u = unique_holders({{"mpv", "a"}, {"MPV", "b"}, {"vlc", ""}});
  ASSERT_EQ(u.size(), 2u);
  EXPECT_EQ(u[0].reason, "a");
  EXPECT_EQ(u[1].name, "vlc");
}
