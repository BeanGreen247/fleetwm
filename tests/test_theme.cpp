#include "theme.hpp"

#include <gtest/gtest.h>
#include <toml++/toml.h>

#include <fstream>

#include "test_util.hpp"

namespace fleetwm {
namespace {

using ThemeTest = testutil::ScopedConfigHome;

// -- parse_hex_color: pure function, no filesystem involved --------------

TEST(ParseHexColor, ValidLowercase) {
  float rgba[4] = {0, 0, 0, 0};
  ASSERT_TRUE(parse_hex_color("#89b4fa", rgba));
  EXPECT_NEAR(rgba[0], 0x89 / 255.0f, 1e-6);
  EXPECT_NEAR(rgba[1], 0xb4 / 255.0f, 1e-6);
  EXPECT_NEAR(rgba[2], 0xfa / 255.0f, 1e-6);
  EXPECT_FLOAT_EQ(rgba[3], 1.0f);
}

TEST(ParseHexColor, ValidUppercase) {
  float rgba[4];
  ASSERT_TRUE(parse_hex_color("#FF00AA", rgba));
  EXPECT_FLOAT_EQ(rgba[0], 1.0f);
  EXPECT_FLOAT_EQ(rgba[1], 0.0f);
}

TEST(ParseHexColor, BlackAndWhite) {
  float rgba[4];
  ASSERT_TRUE(parse_hex_color("#000000", rgba));
  EXPECT_FLOAT_EQ(rgba[0], 0.0f);
  EXPECT_FLOAT_EQ(rgba[1], 0.0f);
  EXPECT_FLOAT_EQ(rgba[2], 0.0f);
  ASSERT_TRUE(parse_hex_color("#ffffff", rgba));
  EXPECT_FLOAT_EQ(rgba[0], 1.0f);
}

TEST(ParseHexColor, MissingHash) {
  float rgba[4];
  EXPECT_FALSE(parse_hex_color("89b4fa", rgba));
}

TEST(ParseHexColor, TooShort) {
  float rgba[4];
  EXPECT_FALSE(parse_hex_color("#89b4f", rgba));
}

TEST(ParseHexColor, TooLong) {
  float rgba[4];
  EXPECT_FALSE(parse_hex_color("#89b4faff", rgba));
}

TEST(ParseHexColor, NonHexDigits) {
  float rgba[4];
  EXPECT_FALSE(parse_hex_color("#zzzzzz", rgba));
}

TEST(ParseHexColor, EmptyString) {
  float rgba[4];
  EXPECT_FALSE(parse_hex_color("", rgba));
}

TEST(ParseHexColor, NamedColorRejected) {
  float rgba[4];
  EXPECT_FALSE(parse_hex_color("red", rgba));
}

// -- ThemeName <-> string round trip --------------------------------------

TEST(ThemeName, RoundTripsAllValues) {
  for (ThemeName name : {ThemeName::Dark, ThemeName::Catppuccin, ThemeName::Dracula,
                          ThemeName::OledBlack, ThemeName::Light}) {
    EXPECT_EQ(theme_name_from_string(theme_name_to_string(name)), name);
  }
}

TEST(ThemeName, UnknownStringFallsBackToDark) {
  EXPECT_EQ(theme_name_from_string("not-a-real-theme"), ThemeName::Dark);
  EXPECT_EQ(theme_name_from_string(""), ThemeName::Dark);
}

TEST(ThemeName, CssFilenameMatchesName) {
  EXPECT_EQ(theme_css_filename(ThemeName::Dracula), "dracula.css");
  EXPECT_EQ(theme_css_filename(ThemeName::OledBlack), "oled_black.css");
}

// -- load_theme_config() / save_theme_config() ----------------------------

TEST_F(ThemeTest, LoadWithNoConfigFileReturnsDefaults) {
  ThemeConfig config = load_theme_config();
  EXPECT_EQ(config.corner_style, CornerStyle::Rounded);
  EXPECT_EQ(config.theme, ThemeName::Dark);
  EXPECT_TRUE(config.accent.auto_extract);
  EXPECT_EQ(config.gap_px, 2);
}

TEST_F(ThemeTest, SaveThenLoadRoundTrips) {
  ThemeConfig config;
  config.corner_style = CornerStyle::Sharp;
  config.theme = ThemeName::Catppuccin;
  config.accent.auto_extract = false;
  config.accent.hex = "#abcdef";
  config.focus_border_thickness_px = 4;
  config.focus_border_color = "#112233";
  config.gap_px = 10;
  config.pinned_border_color = "#445566";
  config.pinned_focused_border_color = "#778899";
  config.pinned_border_thickness_px = 5;

  save_theme_config(config);
  ThemeConfig loaded = load_theme_config();

  EXPECT_EQ(loaded.corner_style, CornerStyle::Sharp);
  EXPECT_EQ(loaded.theme, ThemeName::Catppuccin);
  EXPECT_FALSE(loaded.accent.auto_extract);
  EXPECT_EQ(loaded.accent.hex, "#abcdef");
  EXPECT_EQ(loaded.focus_border_thickness_px, 4);
  EXPECT_EQ(loaded.focus_border_color, "#112233");
  EXPECT_EQ(loaded.gap_px, 10);
  EXPECT_EQ(loaded.pinned_border_color, "#445566");
  EXPECT_EQ(loaded.pinned_focused_border_color, "#778899");
  EXPECT_EQ(loaded.pinned_border_thickness_px, 5);
}

TEST_F(ThemeTest, AutoAccentRoundTrips) {
  ThemeConfig config;
  config.accent.auto_extract = true;
  config.accent.hex = "#000000";  // should be ignored on write when auto
  save_theme_config(config);

  ThemeConfig loaded = load_theme_config();
  EXPECT_TRUE(loaded.accent.auto_extract);
}

TEST_F(ThemeTest, PartialConfigKeepsDefaultsForMissingKeys) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "theme.toml");
  out << "theme = \"dracula\"\n";
  out.close();

  ThemeConfig config = load_theme_config();
  EXPECT_EQ(config.theme, ThemeName::Dracula);
  // Everything else should still be the ThemeConfig{} default.
  EXPECT_EQ(config.corner_style, CornerStyle::Rounded);
  EXPECT_EQ(config.gap_px, 2);
  EXPECT_TRUE(config.accent.auto_extract);
}

TEST_F(ThemeTest, WrongTypeInFieldIsIgnoredNotCrashed) {
  // accent as an integer instead of a string: value<std::string>() should
  // fail to convert and leave the field at its default rather than
  // throwing or reading garbage.
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "theme.toml");
  out << "accent = 42\n";
  out.close();

  ThemeConfig config = load_theme_config();
  EXPECT_TRUE(config.accent.auto_extract);
  EXPECT_EQ(config.accent.hex, "#89b4fa");
}

TEST_F(ThemeTest, MalformedTomlThrows) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "theme.toml");
  out << "this is not [ valid toml\n";
  out.close();

  EXPECT_THROW(load_theme_config(), toml::parse_error);
}

TEST_F(ThemeTest, EmptyConfigFileYieldsDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream(dir_ / "fleetwm" / "theme.toml").close();

  ThemeConfig config = load_theme_config();
  EXPECT_EQ(config.theme, ThemeName::Dark);
}

TEST(ThemeHome, MissingHomeAndXdgConfigHomeThrows) {
  ::unsetenv("XDG_CONFIG_HOME");
  const char* old_home = std::getenv("HOME");
  std::string old_home_copy = old_home ? old_home : "";
  ::unsetenv("HOME");

  EXPECT_THROW(user_config_path(), std::runtime_error);

  if (!old_home_copy.empty()) {
    ::setenv("HOME", old_home_copy.c_str(), 1);
  }
}

}  // namespace
}  // namespace fleetwm
