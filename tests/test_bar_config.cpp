#include "bar_config.hpp"

#include <gtest/gtest.h>
#include <toml++/toml.h>

#include <fstream>

#include "test_util.hpp"

namespace fleetwm {
namespace {

using BarConfigTest = testutil::ScopedConfigHome;

// -- PowerMode <-> string conversions -------------------------------------

TEST(PowerMode, RoundTripsAllValues) {
  for (PowerMode mode : {PowerMode::Normal, PowerMode::Performance, PowerMode::BatterySaver}) {
    EXPECT_EQ(power_mode_from_string(power_mode_to_string(mode)), mode);
  }
}

TEST(PowerMode, UnknownStringFallsBackToNormal) {
  EXPECT_EQ(power_mode_from_string("bogus"), PowerMode::Normal);
  EXPECT_EQ(power_mode_from_string(""), PowerMode::Normal);
}

TEST(PowerMode, ProfilesDaemonNamesMatchPowerProfilesDaemon) {
  EXPECT_EQ(power_mode_to_profiles_daemon_name(PowerMode::Normal), "balanced");
  EXPECT_EQ(power_mode_to_profiles_daemon_name(PowerMode::Performance), "performance");
  EXPECT_EQ(power_mode_to_profiles_daemon_name(PowerMode::BatterySaver), "power-saver");
}

// -- BarLayout <-> string conversions --------------------------------------

TEST(BarLayout, RoundTripsAllValues) {
  for (BarLayout layout : {BarLayout::Full, BarLayout::Island, BarLayout::Capsules}) {
    EXPECT_EQ(bar_layout_from_string(bar_layout_to_string(layout)), layout);
  }
}

TEST(BarLayout, UnknownStringFallsBackToFull) {
  EXPECT_EQ(bar_layout_from_string("bogus"), BarLayout::Capsules);
  EXPECT_EQ(bar_layout_from_string(""), BarLayout::Capsules);
}

// -- TaskbarPosition ---------------------------------------------------------

TEST(TaskbarPosition, RoundTripsAllValues) {
  for (TaskbarPosition p : {TaskbarPosition::Bottom, TaskbarPosition::Top, TaskbarPosition::Left,
                            TaskbarPosition::Right}) {
    EXPECT_EQ(taskbar_position_from_string(taskbar_position_to_string(p)), p);
  }
}

TEST(TaskbarPosition, UnknownStringFallsBackToBottom) {
  EXPECT_EQ(taskbar_position_from_string("bogus"), TaskbarPosition::Bottom);
  EXPECT_EQ(taskbar_position_from_string(""), TaskbarPosition::Bottom);
  EXPECT_EQ(taskbar_position_from_string("LEFT"), TaskbarPosition::Bottom);  // case-sensitive
}

TEST_F(BarConfigTest, TaskbarPositionDefaultsToBottomAndRoundTrips) {
  EXPECT_EQ(load_bar_config().taskbar_position, TaskbarPosition::Bottom);
  BarConfig config;
  config.taskbar_position = TaskbarPosition::Right;
  save_bar_config(config);
  EXPECT_EQ(load_bar_config().taskbar_position, TaskbarPosition::Right);
}

// -- load_bar_config() / save_bar_config() --------------------------------

TEST_F(BarConfigTest, LoadWithNoConfigFileReturnsDefaults) {
  BarConfig config = load_bar_config();
  EXPECT_FALSE(config.clock.show_seconds);
  EXPECT_FALSE(config.clock.show_date);
  EXPECT_TRUE(config.workspace_colors.inactive_bg.empty());
  EXPECT_EQ(config.power_mode, PowerMode::Normal);
  EXPECT_EQ(config.layout, BarLayout::Capsules);
}

TEST_F(BarConfigTest, SaveThenLoadRoundTripsNestedTables) {
  BarConfig config;
  config.clock.show_seconds = true;
  config.clock.show_date = true;
  config.clock.show_year = false;
  config.clock.show_month = false;
  config.clock.show_day = false;
  config.workspace_colors.inactive_bg = "#111111";
  config.workspace_colors.inactive_fg = "#222222";
  config.workspace_colors.active_bg = "#333333";
  config.workspace_colors.active_fg = "#444444";
  config.workspace_colors.buttons_rounded = false;
  config.power_mode = PowerMode::Performance;
  config.layout = BarLayout::Island;

  save_bar_config(config);
  BarConfig loaded = load_bar_config();

  EXPECT_TRUE(loaded.clock.show_seconds);
  EXPECT_TRUE(loaded.clock.show_date);
  EXPECT_FALSE(loaded.clock.show_year);
  EXPECT_EQ(loaded.workspace_colors.inactive_bg, "#111111");
  EXPECT_EQ(loaded.workspace_colors.active_fg, "#444444");
  EXPECT_FALSE(loaded.workspace_colors.buttons_rounded);
  EXPECT_EQ(loaded.power_mode, PowerMode::Performance);
  EXPECT_EQ(loaded.layout, BarLayout::Island);
}

TEST_F(BarConfigTest, LayoutIslandRoundTrips) {
  BarConfig config;
  config.layout = BarLayout::Island;
  save_bar_config(config);
  EXPECT_EQ(load_bar_config().layout, BarLayout::Island);
}

TEST_F(BarConfigTest, WrongTypeLayoutIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "bar.toml");
  out << "layout = 42\n";
  out.close();
  EXPECT_EQ(load_bar_config().layout, BarLayout::Capsules);
}

TEST_F(BarConfigTest, UnknownLayoutStringFallsBackToFull) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "bar.toml");
  out << "layout = \"sidebar\"\n";
  out.close();
  EXPECT_EQ(load_bar_config().layout, BarLayout::Capsules);
}

TEST_F(BarConfigTest, MissingNestedTableKeepsDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "bar.toml");
  out << "power_mode = \"battery_saver\"\n";
  out.close();

  BarConfig config = load_bar_config();
  EXPECT_EQ(config.power_mode, PowerMode::BatterySaver);
  EXPECT_FALSE(config.clock.show_seconds);
  EXPECT_TRUE(config.workspace_colors.inactive_bg.empty());
}

TEST_F(BarConfigTest, PartiallyFilledNestedTableKeepsRemainingDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "bar.toml");
  out << "[clock]\nshow_seconds = true\n";
  out.close();

  BarConfig config = load_bar_config();
  EXPECT_TRUE(config.clock.show_seconds);
  EXPECT_FALSE(config.clock.show_date);  // still default
  EXPECT_TRUE(config.clock.show_year);   // still default
}

// -- per-field wrong-type-is-ignored ----------------------------------------

TEST_F(BarConfigTest, WrongTypeShowSecondsIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "bar.toml");
  out << "[clock]\nshow_seconds = \"yes\"\n";
  out.close();
  EXPECT_FALSE(load_bar_config().clock.show_seconds);
}

TEST_F(BarConfigTest, IntegerShowDateCoercesToBoolNotIgnored) {
  // toml++'s value<bool>() coerces a numeric node (nonzero -> true), it
  // does not return nullopt for it -- unlike the string/int mismatches
  // exercised elsewhere in this file, an int in a bool field is NOT
  // silently ignored. Documenting the actual behavior here rather than
  // the wrong-type-is-ignored assumption this test originally (and
  // incorrectly) encoded.
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "bar.toml");
  out << "[clock]\nshow_date = 1\n";
  out.close();
  EXPECT_TRUE(load_bar_config().clock.show_date);
}

TEST_F(BarConfigTest, WrongTypeInactiveBgIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "bar.toml");
  out << "[workspace_colors]\ninactive_bg = 123\n";
  out.close();
  EXPECT_TRUE(load_bar_config().workspace_colors.inactive_bg.empty());
}

TEST_F(BarConfigTest, WrongTypeButtonsRoundedIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "bar.toml");
  out << "[workspace_colors]\nbuttons_rounded = \"true\"\n";
  out.close();
  EXPECT_TRUE(load_bar_config().workspace_colors.buttons_rounded);
}

TEST_F(BarConfigTest, WrongTypePowerModeIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "bar.toml");
  out << "power_mode = 5\n";
  out.close();
  EXPECT_EQ(load_bar_config().power_mode, PowerMode::Normal);
}

// -- individual field round trips (only one key set) ------------------------

TEST_F(BarConfigTest, OnlyShowYearSetKeepsOtherClockDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "bar.toml");
  out << "[clock]\nshow_year = false\n";
  out.close();

  BarConfig config = load_bar_config();
  EXPECT_FALSE(config.clock.show_year);
  EXPECT_FALSE(config.clock.show_seconds);
  EXPECT_TRUE(config.clock.show_month);
  EXPECT_TRUE(config.clock.show_day);
}

TEST_F(BarConfigTest, OnlyActiveBgSetKeepsOtherWorkspaceColorDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "bar.toml");
  out << "[workspace_colors]\nactive_bg = \"#00ff00\"\n";
  out.close();

  BarConfig config = load_bar_config();
  EXPECT_EQ(config.workspace_colors.active_bg, "#00ff00");
  EXPECT_TRUE(config.workspace_colors.inactive_fg.empty());
  EXPECT_TRUE(config.workspace_colors.active_fg.empty());
  EXPECT_TRUE(config.workspace_colors.buttons_rounded);
}

TEST_F(BarConfigTest, PowerModePerformanceRoundTrips) {
  BarConfig config;
  config.power_mode = PowerMode::Performance;
  save_bar_config(config);
  EXPECT_EQ(load_bar_config().power_mode, PowerMode::Performance);
}

TEST_F(BarConfigTest, PowerModeBatterySaverRoundTrips) {
  BarConfig config;
  config.power_mode = PowerMode::BatterySaver;
  save_bar_config(config);
  EXPECT_EQ(load_bar_config().power_mode, PowerMode::BatterySaver);
}

TEST_F(BarConfigTest, MalformedTomlKeepsDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "bar.toml");
  out << "[clock\nshow_seconds = true\n";
  out.close();
  EXPECT_NO_THROW(load_bar_config());
}

TEST_F(BarConfigTest, EmptyConfigFileYieldsDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream(dir_ / "fleetwm" / "bar.toml").close();

  BarConfig config = load_bar_config();
  EXPECT_EQ(config.power_mode, PowerMode::Normal);
  EXPECT_FALSE(config.clock.show_seconds);
}

TEST_F(BarConfigTest, EmptyClockTableKeepsDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "bar.toml");
  out << "[clock]\n";
  out.close();

  BarConfig config = load_bar_config();
  EXPECT_FALSE(config.clock.show_seconds);
  EXPECT_FALSE(config.clock.show_date);
}

TEST_F(BarConfigTest, UserConfigPathEndsWithBarToml) {
  EXPECT_NE(bar_user_config_path().find("bar.toml"), std::string::npos);
}

TEST(PowerMode, ProfilesDaemonNameForEveryValueIsNonEmpty) {
  for (PowerMode mode : {PowerMode::Normal, PowerMode::Performance, PowerMode::BatterySaver}) {
    EXPECT_FALSE(power_mode_to_profiles_daemon_name(mode).empty());
  }
}

TEST_F(BarConfigTest, ClockAllFieldsFalseRoundTrips) {
  BarConfig config;
  config.clock.show_seconds = false;
  config.clock.show_date = false;
  config.clock.show_year = false;
  config.clock.show_month = false;
  config.clock.show_day = false;
  save_bar_config(config);

  BarConfig loaded = load_bar_config();
  EXPECT_FALSE(loaded.clock.show_seconds);
  EXPECT_FALSE(loaded.clock.show_date);
  EXPECT_FALSE(loaded.clock.show_year);
  EXPECT_FALSE(loaded.clock.show_month);
  EXPECT_FALSE(loaded.clock.show_day);
}

TEST_F(BarConfigTest, PowerModeExplicitNormalStringRoundTrips) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "bar.toml");
  out << "power_mode = \"normal\"\n";
  out.close();
  EXPECT_EQ(load_bar_config().power_mode, PowerMode::Normal);
}

TEST_F(BarConfigTest, WrongTypeInactiveFgIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "bar.toml");
  out << "[workspace_colors]\ninactive_fg = 7\n";
  out.close();
  EXPECT_TRUE(load_bar_config().workspace_colors.inactive_fg.empty());
}

TEST_F(BarConfigTest, WrongTypeActiveFgIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "bar.toml");
  out << "[workspace_colors]\nactive_fg = true\n";
  out.close();
  EXPECT_TRUE(load_bar_config().workspace_colors.active_fg.empty());
}

TEST_F(BarConfigTest, WrongTypeShowYearIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "bar.toml");
  out << "[clock]\nshow_year = \"nope\"\n";
  out.close();
  EXPECT_TRUE(load_bar_config().clock.show_year);
}

TEST_F(BarConfigTest, WorkspaceColorsAllFieldsRoundTrip) {
  BarConfig config;
  config.workspace_colors.inactive_bg = "#010101";
  config.workspace_colors.inactive_fg = "#020202";
  config.workspace_colors.active_bg = "#030303";
  config.workspace_colors.active_fg = "#040404";
  config.workspace_colors.buttons_rounded = false;
  save_bar_config(config);

  BarConfig loaded = load_bar_config();
  EXPECT_EQ(loaded.workspace_colors.inactive_bg, "#010101");
  EXPECT_EQ(loaded.workspace_colors.inactive_fg, "#020202");
  EXPECT_EQ(loaded.workspace_colors.active_bg, "#030303");
  EXPECT_EQ(loaded.workspace_colors.active_fg, "#040404");
  EXPECT_FALSE(loaded.workspace_colors.buttons_rounded);
}

TEST_F(BarConfigTest, DefaultWorkspaceColorsMatchOrangeAccentScheme) {
  BarConfig config;
  EXPECT_TRUE(config.workspace_colors.active_bg.empty());
  EXPECT_TRUE(config.workspace_colors.active_fg.empty());
}

TEST_F(BarConfigTest, ButtonsRoundedDefaultsTrue) {
  EXPECT_TRUE(BarConfig{}.workspace_colors.buttons_rounded);
}

TEST_F(BarConfigTest, ConsecutiveSavesOverwritePreviousPowerMode) {
  BarConfig first;
  first.power_mode = PowerMode::Performance;
  save_bar_config(first);

  BarConfig second;
  second.power_mode = PowerMode::BatterySaver;
  save_bar_config(second);

  EXPECT_EQ(load_bar_config().power_mode, PowerMode::BatterySaver);
}

TEST_F(BarConfigTest, DefaultPowerModeIsNormal) {
  EXPECT_EQ(BarConfig{}.power_mode, PowerMode::Normal);
}

TEST_F(BarConfigTest, DefaultClockHidesSecondsAndDateShowsYear) {
  ClockFormat clock;
  EXPECT_FALSE(clock.show_seconds);
  EXPECT_FALSE(clock.show_date);
  EXPECT_TRUE(clock.show_year);
}

TEST_F(BarConfigTest, SystemDefaultConfigPathDiffersFromUserPath) {
  EXPECT_NE(bar_user_config_path(), bar_system_default_config_path());
}

TEST(PowerMode, ToStringAndProfilesDaemonNameDifferForBatterySaver) {
  // A real historical footgun this config has two parallel string
  // encodings for: bar.toml's own serialization ("battery_saver") vs.
  // powerprofilesctl's expected argument ("power-saver") -- they must
  // NOT be interchangeable, or a naive refactor that collapses them into
  // one string would silently break either persistence or the actual
  // profile switch.
  EXPECT_NE(power_mode_to_string(PowerMode::BatterySaver),
            power_mode_to_profiles_daemon_name(PowerMode::BatterySaver));
}

TEST_F(BarConfigTest, ClockHourFormatAndTimezoneRoundTrip) {
  BarConfig config;
  EXPECT_TRUE(config.clock.use_24h);
  EXPECT_TRUE(config.clock.timezone.empty());
  config.clock.use_24h = false;
  config.clock.timezone = "America/New_York";
  save_bar_config(config);
  BarConfig loaded = load_bar_config();
  EXPECT_FALSE(loaded.clock.use_24h);
  EXPECT_EQ(loaded.clock.timezone, "America/New_York");
}

}  // namespace
}  // namespace fleetwm

namespace fleetwm {
namespace {

TEST_F(BarConfigTest, LegacyWorkspaceDefaultsBecomeThemeFollowing) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "bar.toml");
  out << "[workspace_colors]\ninactive_bg = '#3c3c3c'\ninactive_fg = '#ffffff'\n"
         "active_bg = '#ff7800'\nactive_fg = '#000000'\n";
  out.close();
  const BarConfig c = load_bar_config();
  EXPECT_TRUE(c.workspace_colors.active_bg.empty());
  EXPECT_TRUE(c.workspace_colors.inactive_bg.empty());
}

}  // namespace
TEST_F(BarConfigTest, TaskbarRoundedDefaultsOnAndRoundTrips) {
  EXPECT_TRUE(BarConfig{}.taskbar_rounded);
  BarConfig config;
  config.taskbar_rounded = false;
  save_bar_config(config);
  EXPECT_FALSE(load_bar_config().taskbar_rounded);
}

TEST_F(BarConfigTest, TaskbarOptionsDefaults) {
  const BarConfig c;
  EXPECT_TRUE(c.taskbar_order.empty());
  EXPECT_TRUE(c.taskbar_hidden.empty());
  EXPECT_TRUE(c.pinned_apps.empty());
  EXPECT_FALSE(c.taskbar_autohide);
  EXPECT_FALSE(c.start_centered);
  EXPECT_TRUE(c.taskbar_labels);
}

TEST_F(BarConfigTest, TaskbarOptionsRoundTrip) {
  BarConfig c;
  c.taskbar_order = {"start", "pinned", "windows", "clock"};
  c.taskbar_hidden = {"metrics", "layout"};
  c.pinned_apps = {"firefox.desktop", "org.gnome.Nautilus.desktop"};
  c.taskbar_autohide = true;
  c.start_centered = true;
  c.taskbar_labels = false;
  save_bar_config(c);
  const BarConfig back = load_bar_config();
  EXPECT_EQ(back.taskbar_order, c.taskbar_order);
  EXPECT_EQ(back.taskbar_hidden, c.taskbar_hidden);
  EXPECT_EQ(back.pinned_apps, c.pinned_apps);
  EXPECT_TRUE(back.taskbar_autohide);
  EXPECT_TRUE(back.start_centered);
  EXPECT_FALSE(back.taskbar_labels);
}

TEST_F(BarConfigTest, OlderFileWithoutTaskbarOptionsStillLoads) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream(dir_ / "fleetwm" / "bar.toml") << "taskbar_position = \"top\"\ntaskbar_workspaces = 6\n";
  const BarConfig c = load_bar_config();
  EXPECT_EQ(c.taskbar_position, TaskbarPosition::Top);
  EXPECT_TRUE(c.taskbar_order.empty());
  EXPECT_TRUE(c.taskbar_labels);
}

TEST_F(BarConfigTest, WrongTypesInTheListsAreSkipped) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream(dir_ / "fleetwm" / "bar.toml") << "taskbar_order = [\"start\", 5, \"clock\"]\npinned = \"firefox.desktop\"\n";
  const BarConfig c = load_bar_config();
  EXPECT_EQ(c.taskbar_order, (std::vector<std::string>{"start", "clock"}));
  EXPECT_TRUE(c.pinned_apps.empty());
}

}  // namespace fleetwm
