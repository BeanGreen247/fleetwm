#include "wallpaper_config.hpp"

#include <gtest/gtest.h>

#include <fstream>

#include "test_util.hpp"

namespace fleetwm {
namespace {

using WallpaperConfigTest = testutil::ScopedConfigHome;

TEST_F(WallpaperConfigTest, LoadWithNoConfigFileReturnsDefaults) {
  WallpaperConfig config = load_wallpaper_config();
  EXPECT_EQ(config.path, "");
  EXPECT_FALSE(config.use_solid_color);
  EXPECT_EQ(config.solid_color, "#1e1e2e");
}

TEST_F(WallpaperConfigTest, SaveThenLoadRoundTrips) {
  WallpaperConfig config;
  config.path = "/home/user/Pictures/wall.png";
  config.use_solid_color = true;
  config.solid_color = "#abcdef";

  save_wallpaper_config(config);
  WallpaperConfig loaded = load_wallpaper_config();

  EXPECT_EQ(loaded.path, "/home/user/Pictures/wall.png");
  EXPECT_TRUE(loaded.use_solid_color);
  EXPECT_EQ(loaded.solid_color, "#abcdef");
}

TEST_F(WallpaperConfigTest, EmptyPathRoundTrips) {
  WallpaperConfig config;
  config.path = "";
  save_wallpaper_config(config);

  WallpaperConfig loaded = load_wallpaper_config();
  EXPECT_EQ(loaded.path, "");
}

TEST_F(WallpaperConfigTest, PathWithSpacesAndUnicodeRoundTrips) {
  WallpaperConfig config;
  config.path = "/home/user/My Pictures/背景 (1).png";
  save_wallpaper_config(config);

  WallpaperConfig loaded = load_wallpaper_config();
  EXPECT_EQ(loaded.path, "/home/user/My Pictures/背景 (1).png");
}

TEST_F(WallpaperConfigTest, PartialConfigKeepsDefaultsForMissingKeys) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "wallpaper.toml");
  out << "use_solid_color = true\n";
  out.close();

  WallpaperConfig config = load_wallpaper_config();
  EXPECT_TRUE(config.use_solid_color);
  EXPECT_EQ(config.path, "");
  EXPECT_EQ(config.solid_color, "#1e1e2e");
}

}  // namespace
}  // namespace fleetwm
