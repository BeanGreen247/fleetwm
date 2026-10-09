#include <gtest/gtest.h>

#include "tick_plan.hpp"

using fleetwm::TickPlan;

TEST(TickPlan, JobsLandOnTheSameSecond) {
  TickPlan p;
  const long t0 = 1000123;  // not on a boundary
  const int clock = p.add(1000, t0), stats = p.add(2000, t0), disk = p.add(5000, t0), bat = p.add(15000, t0);
  EXPECT_EQ(p.wait_ms(t0), 877);
  EXPECT_EQ(p.take_due(1001000), (std::vector<int>{clock}));
  EXPECT_EQ(p.take_due(1002000), (std::vector<int>{clock, stats}));
  p.take_due(1003000);
  p.take_due(1004000);
  EXPECT_EQ(p.take_due(1005000), (std::vector<int>{clock, disk, bat}));  // 1005000 is a multiple of 5000 and 15000
  for (long t = 1006000; t < 1015000; t += 1000) p.take_due(t);
  EXPECT_EQ(p.take_due(1015000), (std::vector<int>{clock, disk}));
  for (long t = 1016000; t < 1020000; t += 1000) p.take_due(t);
  EXPECT_EQ(p.take_due(1020000), (std::vector<int>{clock, stats, disk, bat}));
}

TEST(TickPlan, OneWakePerSecondForAllJobs) {
  TickPlan p;
  long now = 5000050;
  p.add(1000, now);
  p.add(2000, now);
  p.add(5000, now);
  p.add(5000, now);
  p.add(15000, now);
  int wakes = 0;
  for (int i = 0; i < 60; ++i) {
    now += p.wait_ms(now);
    ASSERT_FALSE(p.take_due(now).empty());
    ++wakes;
  }
  EXPECT_EQ(wakes, 60);  // 60 seconds, 60 wakes, not 60 + 30 + 12 + 12 + 4
}

TEST(TickPlan, LateWakeCatchesUpOnce) {
  TickPlan p;
  p.add(1000, 0);
  EXPECT_EQ(p.take_due(3400).size(), 1u);  // slept through three boundaries: one run, not three
  EXPECT_EQ(p.wait_ms(3400), 600);
}

TEST(TickPlan, OffJobsNeverFireAndPeriodCanChange) {
  TickPlan p;
  const int a = p.add(0, 100);
  EXPECT_EQ(p.wait_ms(100), -1);
  EXPECT_TRUE(p.take_due(1000000).empty());
  p.set_period(a, 60000, 61000);
  EXPECT_EQ(p.wait_ms(61000), 59000);
  EXPECT_EQ(p.take_due(120000), (std::vector<int>{a}));
  p.set_period(a, 0, 120001);
  EXPECT_EQ(p.wait_ms(120001), -1);
}

TEST(TickPlan, WaitIsNeverZero) {
  TickPlan p;
  p.add(1000, 0);
  EXPECT_EQ(p.wait_ms(5000), 1);  // already overdue: wake at once
}
