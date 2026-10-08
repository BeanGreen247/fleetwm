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
  EXPECT_EQ(config.gap_px, 8);
}

TEST_F(ThemeTest, SettingsArePerUser) {
  ThemeConfig mine;
  mine.gap_px = 3;
  mine.window_layout = WindowLayout::Desktop;
  mine.titlebar.height = 44;
  save_theme_config(mine);

  // Another user (different XDG_CONFIG_HOME) starts from defaults and does not see it.
  const auto other = dir_ / "other-user";
  std::filesystem::create_directories(other);
  ::setenv("XDG_CONFIG_HOME", other.c_str(), 1);
  const ThemeConfig theirs = load_theme_config();
  EXPECT_EQ(theirs.gap_px, 8);
  EXPECT_EQ(theirs.window_layout, WindowLayout::Tiling);
  EXPECT_EQ(theirs.titlebar.height, 32);

  // Switching back finds the first user's file untouched.
  ::setenv("XDG_CONFIG_HOME", dir_.c_str(), 1);
  const ThemeConfig back = load_theme_config();
  EXPECT_EQ(back.gap_px, 3);
  EXPECT_EQ(back.window_layout, WindowLayout::Desktop);
  EXPECT_EQ(back.titlebar.height, 44);
}

TEST_F(ThemeTest, TitlebarDefaults) {
  const TitlebarConfig tb = load_theme_config().titlebar;
  EXPECT_EQ(tb.height, 32);
  EXPECT_EQ(tb.button_width, 38);
  EXPECT_EQ(tb.button_height, 24);
  EXPECT_EQ(tb.buttons_side, ButtonSide::Right);
  EXPECT_EQ(tb.title_align, TitleAlign::Center);
  EXPECT_TRUE(tb.show_pin);
  EXPECT_TRUE(tb.show_minimize);
  EXPECT_TRUE(tb.show_maximize);
}

TEST_F(ThemeTest, TitlebarRoundTripsEveryField) {
  ThemeConfig c;
  c.titlebar = {40, 52, 30, ButtonSide::Left, TitleAlign::Right, false, false, true};
  save_theme_config(c);
  const TitlebarConfig tb = load_theme_config().titlebar;
  EXPECT_EQ(tb.height, 40);
  EXPECT_EQ(tb.button_width, 52);
  EXPECT_EQ(tb.button_height, 30);
  EXPECT_EQ(tb.buttons_side, ButtonSide::Left);
  EXPECT_EQ(tb.title_align, TitleAlign::Right);
  EXPECT_FALSE(tb.show_pin);
  EXPECT_FALSE(tb.show_minimize);
  EXPECT_TRUE(tb.show_maximize);
}

TEST_F(ThemeTest, WindowFrameWidthDefaultsRoundTripsAndIsClamped) {
  EXPECT_EQ(load_theme_config().titlebar.frame_px, 8);
  ThemeConfig c;
  c.titlebar.frame_px = 9;
  save_theme_config(c);
  EXPECT_EQ(load_theme_config().titlebar.frame_px, 9);
  write_config("theme.toml", "[titlebar]\nframe_px = 99\n");
  EXPECT_EQ(load_theme_config().titlebar.frame_px, 16);
  write_config("theme.toml", "[titlebar]\nframe_px = -4\n");
  EXPECT_EQ(load_theme_config().titlebar.frame_px, 0);
}

TEST_F(ThemeTest, TitlebarValuesAreClamped) {
  write_config("theme.toml", "[titlebar]\nheight = 500\nbutton_width = 2\nbutton_height = 900\n");
  const TitlebarConfig tb = load_theme_config().titlebar;
  EXPECT_EQ(tb.height, 64);
  EXPECT_EQ(tb.button_width, 20);
  EXPECT_EQ(tb.button_height, 64);  // never taller than the titlebar
}

TEST_F(ThemeTest, TitlebarButtonHeightFollowsAShorterTitlebar) {
  write_config("theme.toml", "[titlebar]\nheight = 22\n");
  const TitlebarConfig tb = load_theme_config().titlebar;
  EXPECT_EQ(tb.height, 22);
  EXPECT_EQ(tb.button_height, 22);  // default 24 clamped to the height
}

TEST_F(ThemeTest, TitlebarUnknownStringsFallBackToDefaults) {
  write_config("theme.toml", "[titlebar]\nbuttons_side = \"top\"\ntitle_align = \"diagonal\"\n");
  const TitlebarConfig tb = load_theme_config().titlebar;
  EXPECT_EQ(tb.buttons_side, ButtonSide::Right);
  EXPECT_EQ(tb.title_align, TitleAlign::Center);
}

TEST_F(ThemeTest, TitlebarSettingsDoNotDisturbTopLevelKeys) {
  ThemeConfig c;
  c.gap_px = 11;
  c.titlebar.height = 44;
  c.window_layout = WindowLayout::Desktop;
  save_theme_config(c);
  const ThemeConfig loaded = load_theme_config();
  EXPECT_EQ(loaded.gap_px, 11);
  EXPECT_EQ(loaded.titlebar.height, 44);
  EXPECT_EQ(loaded.window_layout, WindowLayout::Desktop);
}

TEST(TitlebarNames, ParseAndFormat) {
  EXPECT_EQ(button_side_from_string("left"), ButtonSide::Left);
  EXPECT_EQ(button_side_from_string("right"), ButtonSide::Right);
  EXPECT_EQ(button_side_from_string(""), ButtonSide::Right);
  EXPECT_EQ(title_align_from_string("left"), TitleAlign::Left);
  EXPECT_EQ(title_align_from_string("center"), TitleAlign::Center);
  EXPECT_EQ(title_align_from_string("right"), TitleAlign::Right);
  EXPECT_EQ(title_align_from_string("middle"), TitleAlign::Center);
  for (ButtonSide s : {ButtonSide::Left, ButtonSide::Right})
    EXPECT_EQ(button_side_from_string(button_side_to_string(s)), s);
  for (TitleAlign a : {TitleAlign::Left, TitleAlign::Center, TitleAlign::Right})
    EXPECT_EQ(title_align_from_string(title_align_to_string(a)), a);
}

TEST_F(ThemeTest, GapDefaults) {
  const ThemeConfig c = load_theme_config();
  EXPECT_EQ(c.gap_px, 8);
  EXPECT_EQ(c.outer_gap_px, 8);
  EXPECT_EQ(c.bar_gap_px, 6);
}

TEST_F(ThemeTest, GapsRoundTripIndependently) {
  ThemeConfig c;
  c.gap_px = 3;
  c.outer_gap_px = 20;
  c.bar_gap_px = 0;
  save_theme_config(c);
  const ThemeConfig loaded = load_theme_config();
  EXPECT_EQ(loaded.gap_px, 3);
  EXPECT_EQ(loaded.outer_gap_px, 20);
  EXPECT_EQ(loaded.bar_gap_px, 0);
}

TEST_F(ThemeTest, OldSingleGapAlsoSetsTheScreenEdgeGap) {
  write_config("theme.toml", "gap_px = 5\n");
  const ThemeConfig c = load_theme_config();
  EXPECT_EQ(c.gap_px, 5);
  EXPECT_EQ(c.outer_gap_px, 5);
  EXPECT_EQ(c.bar_gap_px, 6);
}

TEST_F(ThemeTest, ExplicitEdgeGapBeatsTheOldSingleGap) {
  write_config("theme.toml", "gap_px = 5\nouter_gap_px = 12\n");
  const ThemeConfig c = load_theme_config();
  EXPECT_EQ(c.gap_px, 5);
  EXPECT_EQ(c.outer_gap_px, 12);
}

TEST_F(ThemeTest, GapsAreClampedToZeroAndSixtyFour) {
  write_config("theme.toml", "gap_px = 900\nouter_gap_px = -4\nbar_gap_px = 99\n");
  const ThemeConfig c = load_theme_config();
  EXPECT_EQ(c.gap_px, 64);
  EXPECT_EQ(c.outer_gap_px, 0);
  EXPECT_EQ(c.bar_gap_px, 64);
}

TEST_F(ThemeTest, WindowLayoutDefaultsToTilingAndRoundTrips) {
  EXPECT_EQ(load_theme_config().window_layout, WindowLayout::Tiling);
  ThemeConfig c;
  c.window_layout = WindowLayout::Desktop;
  save_theme_config(c);
  EXPECT_EQ(load_theme_config().window_layout, WindowLayout::Desktop);
}

TEST(WindowLayoutNames, ParseAndFormat) {
  EXPECT_EQ(window_layout_from_string("desktop"), WindowLayout::Desktop);
  EXPECT_EQ(window_layout_from_string("tiling"), WindowLayout::Tiling);
  EXPECT_EQ(window_layout_from_string("garbage"), WindowLayout::Tiling);
  EXPECT_EQ(window_layout_to_string(WindowLayout::Desktop), "desktop");
  EXPECT_EQ(window_layout_to_string(WindowLayout::Tiling), "tiling");
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
  config.render_mode = RenderMode::Custom;
  config.custom_fps_lock = 144;
  config.show_debug_overlay_on_startup = true;

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
  EXPECT_EQ(loaded.render_mode, RenderMode::Custom);
  EXPECT_EQ(loaded.custom_fps_lock, 144);
  EXPECT_TRUE(loaded.show_debug_overlay_on_startup);
}

TEST_F(ThemeTest, RenderModeDefaultsToSynced) {
  ThemeConfig config = load_theme_config();
  EXPECT_EQ(config.render_mode, RenderMode::Synced);
  EXPECT_EQ(config.custom_fps_lock, 60);
  EXPECT_FALSE(config.show_debug_overlay_on_startup);
}

TEST_F(ThemeTest, ShowDebugOverlayOnStartupRoundTripsFalse) {
  ThemeConfig config;
  config.show_debug_overlay_on_startup = true;
  save_theme_config(config);
  EXPECT_TRUE(load_theme_config().show_debug_overlay_on_startup);

  config.show_debug_overlay_on_startup = false;
  save_theme_config(config);
  EXPECT_FALSE(load_theme_config().show_debug_overlay_on_startup);
}

TEST_F(ThemeTest, CustomFpsLockClampsToValidRange) {
  ThemeConfig config;
  config.render_mode = RenderMode::Custom;
  config.custom_fps_lock = 9000;
  save_theme_config(config);
  EXPECT_EQ(load_theme_config().custom_fps_lock, 5000);

  config.custom_fps_lock = 1;
  save_theme_config(config);
  EXPECT_EQ(load_theme_config().custom_fps_lock, 24);
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
  EXPECT_EQ(config.gap_px, 8);
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

// -- more parse_hex_color edge cases --------------------------------------

TEST(ParseHexColor, MixedCase) {
  float rgba[4];
  ASSERT_TRUE(parse_hex_color("#Ff00aA", rgba));
  EXPECT_FLOAT_EQ(rgba[0], 1.0f);
  EXPECT_FLOAT_EQ(rgba[1], 0.0f);
}

TEST(ParseHexColor, JustHashNoDigits) {
  float rgba[4];
  EXPECT_FALSE(parse_hex_color("#", rgba));
}

TEST(ParseHexColor, WhitespaceInside) {
  float rgba[4];
  EXPECT_FALSE(parse_hex_color("#89 4fa", rgba));
}

TEST(ParseHexColor, TrailingWhitespace) {
  float rgba[4];
  EXPECT_FALSE(parse_hex_color("#89b4fa \n", rgba));
}

TEST(ParseHexColor, LowestNonzeroChannelValue) {
  float rgba[4];
  ASSERT_TRUE(parse_hex_color("#010101", rgba));
  EXPECT_NEAR(rgba[0], 1.0f / 255.0f, 1e-6);
}

TEST(ParseHexColor, HighestBelowMaxChannelValue) {
  float rgba[4];
  ASSERT_TRUE(parse_hex_color("#fefefe", rgba));
  EXPECT_NEAR(rgba[0], 0xfe / 255.0f, 1e-6);
}

// -- ThemeName individual mappings -----------------------------------------

TEST(ThemeName, DarkStringMapping) {
  EXPECT_EQ(theme_name_to_string(ThemeName::Dark), "dark");
  EXPECT_EQ(theme_name_from_string("dark"), ThemeName::Dark);
}

TEST(ThemeName, CatppuccinStringMapping) {
  EXPECT_EQ(theme_name_to_string(ThemeName::Catppuccin), "catppuccin");
  EXPECT_EQ(theme_name_from_string("catppuccin"), ThemeName::Catppuccin);
}

TEST(ThemeName, DraculaStringMapping) {
  EXPECT_EQ(theme_name_to_string(ThemeName::Dracula), "dracula");
  EXPECT_EQ(theme_name_from_string("dracula"), ThemeName::Dracula);
}

TEST(ThemeName, OledBlackStringMapping) {
  EXPECT_EQ(theme_name_to_string(ThemeName::OledBlack), "oled_black");
  EXPECT_EQ(theme_name_from_string("oled_black"), ThemeName::OledBlack);
}

TEST(ThemeName, LightStringMapping) {
  EXPECT_EQ(theme_name_to_string(ThemeName::Light), "light");
  EXPECT_EQ(theme_name_from_string("light"), ThemeName::Light);
}

TEST(ThemeName, CssFilenameMatchesEveryTheme) {
  EXPECT_EQ(theme_css_filename(ThemeName::Dark), "dark.css");
  EXPECT_EQ(theme_css_filename(ThemeName::Catppuccin), "catppuccin.css");
  EXPECT_EQ(theme_css_filename(ThemeName::Light), "light.css");
}

// -- CornerStyle round trip --------------------------------------------------

TEST_F(ThemeTest, CornerStyleSharpRoundTrips) {
  ThemeConfig config;
  config.corner_style = CornerStyle::Sharp;
  save_theme_config(config);
  EXPECT_EQ(load_theme_config().corner_style, CornerStyle::Sharp);
}

TEST_F(ThemeTest, CornerStyleRoundedRoundTrips) {
  ThemeConfig config;
  config.corner_style = CornerStyle::Rounded;
  save_theme_config(config);
  EXPECT_EQ(load_theme_config().corner_style, CornerStyle::Rounded);
}

TEST_F(ThemeTest, UnknownCornerStyleStringFallsBackToRounded) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "theme.toml");
  out << "corner_style = \"triangular\"\n";
  out.close();

  EXPECT_EQ(load_theme_config().corner_style, CornerStyle::Rounded);
}

// -- per-field wrong-type-is-ignored ----------------------------------------

TEST_F(ThemeTest, WrongTypeCornerStyleIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "theme.toml");
  out << "corner_style = 1\n";
  out.close();
  EXPECT_EQ(load_theme_config().corner_style, CornerStyle::Rounded);
}

TEST_F(ThemeTest, WrongTypeThemeIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "theme.toml");
  out << "theme = true\n";
  out.close();
  EXPECT_EQ(load_theme_config().theme, ThemeName::Dark);
}

TEST_F(ThemeTest, WrongTypeFocusBorderThicknessIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "theme.toml");
  out << "focus_border_thickness_px = \"thick\"\n";
  out.close();
  EXPECT_EQ(load_theme_config().focus_border_thickness_px, 4);
}

TEST_F(ThemeTest, WrongTypeFocusBorderColorIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "theme.toml");
  out << "focus_border_color = 123\n";
  out.close();
  EXPECT_EQ(load_theme_config().focus_border_color, "#e6e6f2");
}

TEST_F(ThemeTest, WrongTypeGapPxIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "theme.toml");
  out << "gap_px = \"two\"\n";
  out.close();
  EXPECT_EQ(load_theme_config().gap_px, 8);
}

TEST_F(ThemeTest, BoolPinnedBorderThicknessCoercesToZeroNotIgnored) {
  // Same toml++ numeric-coercion behavior as bar_config's
  // IntegerShowDateCoercesToBoolNotIgnored -- value<int64_t>() on a bool
  // node returns 0/1 rather than nullopt, so this is NOT ignored the way
  // a genuinely incompatible type (e.g. a string) would be.
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "theme.toml");
  out << "pinned_border_thickness_px = false\n";
  out.close();
  EXPECT_EQ(load_theme_config().pinned_border_thickness_px, 0);
}

// -- individual field round trips (partial config, only one key set) -------

TEST_F(ThemeTest, OnlyGapPxSetKeepsOtherDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "theme.toml");
  out << "gap_px = 7\n";
  out.close();

  ThemeConfig config = load_theme_config();
  EXPECT_EQ(config.gap_px, 7);
  EXPECT_EQ(config.pinned_border_thickness_px, 4);
  EXPECT_EQ(config.focus_border_thickness_px, 4);
}

TEST_F(ThemeTest, OnlyPinnedBorderColorSetKeepsOtherDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "theme.toml");
  out << "pinned_border_color = \"#abcdef\"\n";
  out.close();

  ThemeConfig config = load_theme_config();
  EXPECT_EQ(config.pinned_border_color, "#abcdef");
  EXPECT_EQ(config.pinned_focused_border_color, "#99e666");
}

TEST_F(ThemeTest, ExplicitAccentAutoStringRoundTrips) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "theme.toml");
  out << "accent = \"auto\"\n";
  out.close();

  ThemeConfig config = load_theme_config();
  EXPECT_TRUE(config.accent.auto_extract);
}

TEST_F(ThemeTest, ExplicitAccentHexDisablesAutoExtract) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "theme.toml");
  out << "accent = \"#123456\"\n";
  out.close();

  ThemeConfig config = load_theme_config();
  EXPECT_FALSE(config.accent.auto_extract);
  EXPECT_EQ(config.accent.hex, "#123456");
}

TEST_F(ThemeTest, ZeroGapPxRoundTrips) {
  ThemeConfig config;
  config.gap_px = 0;
  save_theme_config(config);
  EXPECT_EQ(load_theme_config().gap_px, 0);
}

TEST_F(ThemeTest, NegativeThicknessRoundTrips) {
  // Not clamped at this layer -- load/save is a pure passthrough; any
  // clamping to a sane visual range happens at the render call site, not
  // here (matches this module's "no validation beyond type-checking"
  // contract, same as every other field).
  ThemeConfig config;
  config.focus_border_thickness_px = -1;
  save_theme_config(config);
  EXPECT_EQ(load_theme_config().focus_border_thickness_px, -1);
}

TEST_F(ThemeTest, ThemesDirIsNonEmpty) {
  EXPECT_FALSE(themes_dir().empty());
}

TEST_F(ThemeTest, UserConfigPathEndsWithThemeToml) {
  std::string path = user_config_path();
  EXPECT_NE(path.find("theme.toml"), std::string::npos);
}

// -- per-channel parse_hex_color isolation ----------------------------------

TEST(ParseHexColor, OnlyRedChannelSet) {
  float rgba[4];
  ASSERT_TRUE(parse_hex_color("#ff0000", rgba));
  EXPECT_FLOAT_EQ(rgba[0], 1.0f);
  EXPECT_FLOAT_EQ(rgba[1], 0.0f);
  EXPECT_FLOAT_EQ(rgba[2], 0.0f);
}

TEST(ParseHexColor, OnlyGreenChannelSet) {
  float rgba[4];
  ASSERT_TRUE(parse_hex_color("#00ff00", rgba));
  EXPECT_FLOAT_EQ(rgba[0], 0.0f);
  EXPECT_FLOAT_EQ(rgba[1], 1.0f);
  EXPECT_FLOAT_EQ(rgba[2], 0.0f);
}

TEST(ParseHexColor, OnlyBlueChannelSet) {
  float rgba[4];
  ASSERT_TRUE(parse_hex_color("#0000ff", rgba));
  EXPECT_FLOAT_EQ(rgba[0], 0.0f);
  EXPECT_FLOAT_EQ(rgba[1], 0.0f);
  EXPECT_FLOAT_EQ(rgba[2], 1.0f);
}

TEST(ParseHexColor, AlphaAlwaysOneRegardlessOfRgb) {
  float rgba[4];
  ASSERT_TRUE(parse_hex_color("#123456", rgba));
  EXPECT_FLOAT_EQ(rgba[3], 1.0f);
  ASSERT_TRUE(parse_hex_color("#000000", rgba));
  EXPECT_FLOAT_EQ(rgba[3], 1.0f);
}

TEST(ParseHexColor, MidToneGray) {
  float rgba[4];
  ASSERT_TRUE(parse_hex_color("#808080", rgba));
  EXPECT_NEAR(rgba[0], 0x80 / 255.0f, 1e-6);
  EXPECT_NEAR(rgba[1], 0x80 / 255.0f, 1e-6);
  EXPECT_NEAR(rgba[2], 0x80 / 255.0f, 1e-6);
}

// -- remaining individual field round trips ---------------------------------

TEST_F(ThemeTest, OnlyFocusBorderColorSetKeepsOtherDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "theme.toml");
  out << "focus_border_color = \"#abcabc\"\n";
  out.close();

  ThemeConfig config = load_theme_config();
  EXPECT_EQ(config.focus_border_color, "#abcabc");
  EXPECT_EQ(config.focus_border_thickness_px, 4);
}

TEST_F(ThemeTest, OnlyPinnedFocusedBorderColorSetKeepsOtherDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "theme.toml");
  out << "pinned_focused_border_color = \"#bada55\"\n";
  out.close();

  ThemeConfig config = load_theme_config();
  EXPECT_EQ(config.pinned_focused_border_color, "#bada55");
  EXPECT_EQ(config.pinned_border_color, "#3399ff");
}

TEST_F(ThemeTest, OnlyPinnedBorderThicknessSetKeepsOtherDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "theme.toml");
  out << "pinned_border_thickness_px = 9\n";
  out.close();

  ThemeConfig config = load_theme_config();
  EXPECT_EQ(config.pinned_border_thickness_px, 9);
  EXPECT_EQ(config.focus_border_thickness_px, 4);
}

TEST_F(ThemeTest, OnlyFocusBorderThicknessSetKeepsOtherDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "theme.toml");
  out << "focus_border_thickness_px = 6\n";
  out.close();

  ThemeConfig config = load_theme_config();
  EXPECT_EQ(config.focus_border_thickness_px, 6);
  EXPECT_EQ(config.pinned_border_thickness_px, 4);
}

TEST_F(ThemeTest, AccentHexAllDigits) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "theme.toml");
  out << "accent = \"#123456\"\n";
  out.close();
  EXPECT_EQ(load_theme_config().accent.hex, "#123456");
}

TEST_F(ThemeTest, AccentHexAllLetters) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "theme.toml");
  out << "accent = \"#abcdef\"\n";
  out.close();
  EXPECT_EQ(load_theme_config().accent.hex, "#abcdef");
}

TEST_F(ThemeTest, LargeGapPxRoundTrips) {
  ThemeConfig config;
  config.gap_px = 500;  // saved as given, clamped to 64 when loaded
  save_theme_config(config);
  EXPECT_EQ(load_theme_config().gap_px, 64);
}

TEST_F(ThemeTest, ZeroThicknessRoundTrips) {
  ThemeConfig config;
  config.focus_border_thickness_px = 0;
  config.pinned_border_thickness_px = 0;
  save_theme_config(config);

  ThemeConfig loaded = load_theme_config();
  EXPECT_EQ(loaded.focus_border_thickness_px, 0);
  EXPECT_EQ(loaded.pinned_border_thickness_px, 0);
}

TEST_F(ThemeTest, SystemDefaultConfigPathDiffersFromUserPath) {
  EXPECT_NE(user_config_path(), system_default_config_path());
}

TEST_F(ThemeTest, ConsecutiveSavesOverwritePreviousTheme) {
  ThemeConfig first;
  first.theme = ThemeName::Dark;
  save_theme_config(first);

  ThemeConfig second;
  second.theme = ThemeName::Light;
  save_theme_config(second);

  EXPECT_EQ(load_theme_config().theme, ThemeName::Light);
}

TEST_F(ThemeTest, DefaultAccentIsAutoExtract) {
  EXPECT_TRUE(ThemeConfig{}.accent.auto_extract);
}

TEST_F(ThemeTest, DefaultGapPxIsEight) {
  EXPECT_EQ(ThemeConfig{}.gap_px, 8);
}

TEST_F(ThemeTest, DefaultCornerStyleIsRounded) {
  EXPECT_EQ(ThemeConfig{}.corner_style, CornerStyle::Rounded);
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
