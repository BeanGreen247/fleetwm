#include <gtest/gtest.h>

#include "frame_stats.hpp"

namespace fleetwm {
namespace {

TEST(FrameStats, EvenFramesHaveNoHitches) {
  FrameStats s;
  for (int i = 0; i < 300; ++i) s.add_interval(6.94);
  const FrameSummary f = s.summary(6.94);
  EXPECT_EQ(f.frames, 300u);
  EXPECT_NEAR(f.fps, 144.1, 0.5);
  EXPECT_NEAR(f.p99, 6.94, 0.01);
  EXPECT_EQ(f.over_1_5, 0u);
  EXPECT_EQ(f.over_3, 0u);
}

TEST(FrameStats, CountsLateAndHitchedFrames) {
  FrameStats s;
  for (int i = 0; i < 97; ++i) s.add_interval(16.7);
  s.add_interval(30.0);   // two refreshes: late
  s.add_interval(60.0);   // three and a half: a hitch (and late)
  s.add_interval(120.0);  // a hitch
  const FrameSummary f = s.summary(16.7);
  EXPECT_EQ(f.over_1_5, 3u);
  EXPECT_EQ(f.over_3, 2u);
  EXPECT_DOUBLE_EQ(f.max_interval, 120.0);
}

TEST(FrameStats, KeepsOnlyTheLatestWindowAndResets) {
  FrameStats s;
  for (size_t i = 0; i < kFrameStatsWindow + 500; ++i) s.add_interval(i < 400 ? 100.0 : 10.0);
  FrameSummary f = s.summary(10.0);
  EXPECT_EQ(f.frames, kFrameStatsWindow);
  EXPECT_DOUBLE_EQ(f.max_interval, 10.0);  // the slow early frames have rotated out
  s.reset();
  EXPECT_EQ(s.summary(10.0).frames, 0u);
}

TEST(FrameStats, TracksTheCompositorsOwnCost) {
  FrameStats s;
  s.add_work(1.0);
  s.add_work(3.0);
  s.add_work(-1.0);  // ignored
  const FrameSummary f = s.summary(0);
  EXPECT_DOUBLE_EQ(f.work_avg, 2.0);
  EXPECT_DOUBLE_EQ(f.work_max, 3.0);
  EXPECT_EQ(f.over_1_5, 0u);  // no refresh known: no late counts
}

}  // namespace
}  // namespace fleetwm
