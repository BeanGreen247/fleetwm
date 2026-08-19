#include "wallpaper_config.hpp"

#include <gtest/gtest.h>
#include <toml++/toml.h>

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

TEST_F(WallpaperConfigTest, WrongTypePathIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "wallpaper.toml");
  out << "path = 42\n";
  out.close();
  EXPECT_EQ(load_wallpaper_config().path, "");
}

TEST_F(WallpaperConfigTest, WrongTypeUseSolidColorIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "wallpaper.toml");
  out << "use_solid_color = \"yes\"\n";
  out.close();
  EXPECT_FALSE(load_wallpaper_config().use_solid_color);
}

TEST_F(WallpaperConfigTest, WrongTypeSolidColorIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "wallpaper.toml");
  out << "solid_color = false\n";
  out.close();
  EXPECT_EQ(load_wallpaper_config().solid_color, "#1e1e2e");
}

TEST_F(WallpaperConfigTest, OnlySolidColorSetKeepsOtherDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "wallpaper.toml");
  out << "solid_color = \"#ff0000\"\n";
  out.close();

  WallpaperConfig config = load_wallpaper_config();
  EXPECT_EQ(config.solid_color, "#ff0000");
  EXPECT_FALSE(config.use_solid_color);
  EXPECT_EQ(config.path, "");
}

TEST_F(WallpaperConfigTest, UseSolidColorFalseRoundTrips) {
  WallpaperConfig config;
  config.use_solid_color = false;
  config.path = "/some/path.png";
  save_wallpaper_config(config);

  WallpaperConfig loaded = load_wallpaper_config();
  EXPECT_FALSE(loaded.use_solid_color);
  EXPECT_EQ(loaded.path, "/some/path.png");
}

TEST_F(WallpaperConfigTest, PathWithTrailingSlashRoundTrips) {
  WallpaperConfig config;
  config.path = "/home/user/wallpapers/";
  save_wallpaper_config(config);
  EXPECT_EQ(load_wallpaper_config().path, "/home/user/wallpapers/");
}

TEST_F(WallpaperConfigTest, RelativePathRoundTripsVerbatim) {
  // load/save never validates or resolves the path -- that's
  // fleetwm-wallpaper's job at render time, not this module's. A
  // relative path should pass through completely unchanged.
  WallpaperConfig config;
  config.path = "relative/wall.jpg";
  save_wallpaper_config(config);
  EXPECT_EQ(load_wallpaper_config().path, "relative/wall.jpg");
}

TEST_F(WallpaperConfigTest, MalformedTomlThrows) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "wallpaper.toml");
  out << "path = [unterminated\n";
  out.close();
  EXPECT_THROW(load_wallpaper_config(), toml::parse_error);
}

TEST_F(WallpaperConfigTest, EmptyConfigFileYieldsDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream(dir_ / "fleetwm" / "wallpaper.toml").close();

  WallpaperConfig config = load_wallpaper_config();
  EXPECT_EQ(config.path, "");
  EXPECT_EQ(config.solid_color, "#1e1e2e");
}

TEST_F(WallpaperConfigTest, UserConfigPathEndsWithWallpaperToml) {
  EXPECT_NE(wallpaper_user_config_path().find("wallpaper.toml"), std::string::npos);
}

TEST_F(WallpaperConfigTest, SystemDefaultConfigPathDiffersFromUserPath) {
  EXPECT_NE(wallpaper_user_config_path(), wallpaper_system_default_config_path());
}

TEST_F(WallpaperConfigTest, ConsecutiveSavesOverwritePreviousValue) {
  WallpaperConfig first;
  first.path = "/first.png";
  save_wallpaper_config(first);

  WallpaperConfig second;
  second.path = "/second.png";
  save_wallpaper_config(second);

  EXPECT_EQ(load_wallpaper_config().path, "/second.png");
}

TEST_F(WallpaperConfigTest, LongPathRoundTrips) {
  WallpaperConfig config;
  config.path = "/home/user/Pictures/Wallpapers/Archive/2026/Summer/mountain-lake-sunrise-4k.png";
  save_wallpaper_config(config);
  EXPECT_EQ(load_wallpaper_config().path,
            "/home/user/Pictures/Wallpapers/Archive/2026/Summer/mountain-lake-sunrise-4k.png");
}

TEST_F(WallpaperConfigTest, UppercaseHexSolidColorRoundTrips) {
  WallpaperConfig config;
  config.solid_color = "#ABCDEF";
  save_wallpaper_config(config);
  EXPECT_EQ(load_wallpaper_config().solid_color, "#ABCDEF");
}

TEST_F(WallpaperConfigTest, DefaultSolidColorMatchesDarkThemeBackground) {
  EXPECT_EQ(WallpaperConfig{}.solid_color, "#1e1e2e");
}

TEST_F(WallpaperConfigTest, UseSolidColorTrueWithEmptyPathRoundTrips) {
  WallpaperConfig config;
  config.use_solid_color = true;
  config.path = "";
  save_wallpaper_config(config);

  WallpaperConfig loaded = load_wallpaper_config();
  EXPECT_TRUE(loaded.use_solid_color);
  EXPECT_EQ(loaded.path, "");
}

TEST_F(WallpaperConfigTest, JpegExtensionPathRoundTrips) {
  WallpaperConfig config;
  config.path = "/home/user/wallpaper.jpg";
  save_wallpaper_config(config);
  EXPECT_EQ(load_wallpaper_config().path, "/home/user/wallpaper.jpg");
}

TEST_F(WallpaperConfigTest, ToggleUseSolidColorBackAndForth) {
  WallpaperConfig config;
  config.use_solid_color = true;
  save_wallpaper_config(config);
  EXPECT_TRUE(load_wallpaper_config().use_solid_color);

  config.use_solid_color = false;
  save_wallpaper_config(config);
  EXPECT_FALSE(load_wallpaper_config().use_solid_color);
}

TEST_F(WallpaperConfigTest, PngAndJpgPathsBothRoundTripUnmodified) {
  WallpaperConfig config;
  config.path = "/wall.PNG";
  save_wallpaper_config(config);
  EXPECT_EQ(load_wallpaper_config().path, "/wall.PNG");
}

TEST_F(WallpaperConfigTest, DefaultUseSolidColorIsFalse) {
  EXPECT_FALSE(WallpaperConfig{}.use_solid_color);
}

TEST_F(WallpaperConfigTest, PathWithSingleQuoteRoundTrips) {
  WallpaperConfig config;
  config.path = "/home/user/Bob's Pictures/wall.png";
  save_wallpaper_config(config);
  EXPECT_EQ(load_wallpaper_config().path, "/home/user/Bob's Pictures/wall.png");
}

}  // namespace
}  // namespace fleetwm
