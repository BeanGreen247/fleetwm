#include "game_config.hpp"

#include <gtest/gtest.h>

#include "test_util.hpp"

namespace fleetwm {
namespace {

using GameConfigTest = testutil::ScopedConfigHome;

TEST_F(GameConfigTest, DefaultsWithoutAFile) {
  const GameConfig c = load_game_config();
  EXPECT_EQ(c.scaling, FullscreenScaling::Fit);
  EXPECT_EQ(c.filter, FullscreenFilter::Auto);
  EXPECT_TRUE(c.adaptive_sync);
  EXPECT_TRUE(c.gamemode);
  EXPECT_TRUE(c.tuxthrottle);
  EXPECT_EQ(c.max_render_time_ms, 0);
  EXPECT_FALSE(c.realtime);
  EXPECT_FALSE(c.mode_switch);
  EXPECT_EQ(c.fps_limit, 0);
  EXPECT_EQ(c.desktop_gpu, DesktopGpu::Integrated);
}

TEST_F(GameConfigTest, RoundTrips) {
  GameConfig c;
  c.scaling = FullscreenScaling::Integer;
  c.filter = FullscreenFilter::Nearest;
  c.adaptive_sync = false;
  c.gamemode = false;
  c.tuxthrottle = false;
  c.max_render_time_ms = 5;
  c.realtime = true;
  c.mode_switch = true;
  c.desktop_gpu = DesktopGpu::Discrete;
  c.fps_limit = 144;
  c.gamemode_apps = {"factorio", "osu"};
  save_game_config(c);
  EXPECT_EQ(load_game_config(), c);
}

TEST_F(GameConfigTest, FpsLimitIsClamped) {
  GameConfig c;
  c.fps_limit = 3;
  save_game_config(c);
  EXPECT_EQ(load_game_config().fps_limit, kFpsLimitMin);
  c.fps_limit = 99999;
  save_game_config(c);
  EXPECT_EQ(load_game_config().fps_limit, kFpsLimitMax);
  c.fps_limit = -5;
  save_game_config(c);
  EXPECT_EQ(load_game_config().fps_limit, 0);
}

TEST_F(GameConfigTest, RenderTimeIsClamped) {
  GameConfig c;
  c.max_render_time_ms = 99;
  save_game_config(c);
  EXPECT_EQ(load_game_config().max_render_time_ms, kRenderTimeMax);
  c.max_render_time_ms = -3;
  save_game_config(c);
  EXPECT_EQ(load_game_config().max_render_time_ms, 0);
}

TEST(GameFit, FourByThreeOnSixteenByNineGetsBars) {
  const FitRect r = fullscreen_fit(FullscreenScaling::Fit, 640, 480, 1920, 1080);
  EXPECT_EQ(r.h, 1080);
  EXPECT_EQ(r.w, 1440);
  EXPECT_EQ(r.x, 240);
  EXPECT_EQ(r.y, 0);
  EXPECT_TRUE(r.scaled);
}

TEST(GameFit, IntegerPicksTheLargestWholeNumber) {
  const FitRect r = fullscreen_fit(FullscreenScaling::Integer, 320, 200, 1920, 1080);
  EXPECT_EQ(r.w, 1600);
  EXPECT_EQ(r.h, 1000);
  EXPECT_EQ(r.x, 160);
  EXPECT_EQ(r.y, 40);
  EXPECT_TRUE(r.integer);
}

TEST(GameFit, StretchFillsTheScreen) {
  const FitRect r = fullscreen_fit(FullscreenScaling::Stretch, 640, 480, 1920, 1080);
  EXPECT_EQ(r.w, 1920);
  EXPECT_EQ(r.h, 1080);
}

TEST(GameFit, SameSizeOrOffIsUntouched) {
  EXPECT_FALSE(fullscreen_fit(FullscreenScaling::Fit, 1920, 1080, 1920, 1080).scaled);
  EXPECT_FALSE(fullscreen_fit(FullscreenScaling::Off, 640, 480, 1920, 1080).scaled);
}

TEST(GameFit, SmallerScreenThanPictureShrinksToFit) {
  const FitRect r = fullscreen_fit(FullscreenScaling::Integer, 1920, 1080, 1280, 720);
  EXPECT_EQ(r.w, 1280);
  EXPECT_EQ(r.h, 720);
}

}  // namespace
}  // namespace fleetwm
