#include "default_apps.hpp"

#include <gtest/gtest.h>
#include <toml++/toml.h>

#include <fstream>

#include "test_util.hpp"

namespace fleetwm {
namespace {

using DefaultAppsTest = testutil::ScopedConfigHome;

TEST_F(DefaultAppsTest, LoadWithNoConfigFileReturnsDefaults) {
  DefaultAppsConfig config = load_default_apps_config();
  EXPECT_EQ(config.terminal_command, "foot");
}

TEST_F(DefaultAppsTest, SaveThenLoadRoundTrips) {
  DefaultAppsConfig config;
  config.terminal_command = "alacritty";
  save_default_apps_config(config);

  DefaultAppsConfig loaded = load_default_apps_config();
  EXPECT_EQ(loaded.terminal_command, "alacritty");
}

TEST_F(DefaultAppsTest, CommandWithArgsRoundTrips) {
  DefaultAppsConfig config;
  config.terminal_command = "wezterm start --always-new-process";
  save_default_apps_config(config);

  DefaultAppsConfig loaded = load_default_apps_config();
  EXPECT_EQ(loaded.terminal_command, "wezterm start --always-new-process");
}

TEST_F(DefaultAppsTest, EmptyConfigFileYieldsDefault) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream(dir_ / "fleetwm" / "default_apps.toml").close();

  DefaultAppsConfig config = load_default_apps_config();
  EXPECT_EQ(config.terminal_command, "foot");
}

TEST_F(DefaultAppsTest, WrongTypeTerminalCommandIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "default_apps.toml");
  out << "terminal_command = 42\n";
  out.close();
  EXPECT_EQ(load_default_apps_config().terminal_command, "foot");
}

TEST_F(DefaultAppsTest, EmptyTerminalCommandRoundTrips) {
  DefaultAppsConfig config;
  config.terminal_command = "";
  save_default_apps_config(config);
  EXPECT_EQ(load_default_apps_config().terminal_command, "");
}

TEST_F(DefaultAppsTest, CommandWithQuotesRoundTrips) {
  DefaultAppsConfig config;
  config.terminal_command = R"(sh -c "echo hi")";
  save_default_apps_config(config);
  EXPECT_EQ(load_default_apps_config().terminal_command, R"(sh -c "echo hi")");
}

TEST_F(DefaultAppsTest, CommandWithUnicodeRoundTrips) {
  DefaultAppsConfig config;
  config.terminal_command = "term --title=端末";
  save_default_apps_config(config);
  EXPECT_EQ(load_default_apps_config().terminal_command, "term --title=端末");
}

TEST_F(DefaultAppsTest, MalformedTomlThrows) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "default_apps.toml");
  out << "terminal_command = \"unterminated\n";
  out.close();
  EXPECT_THROW(load_default_apps_config(), toml::parse_error);
}

TEST_F(DefaultAppsTest, UnrelatedKeyIsIgnoredNotFatal) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "default_apps.toml");
  out << "unknown_future_key = \"whatever\"\n";
  out.close();
  EXPECT_EQ(load_default_apps_config().terminal_command, "foot");
}

TEST_F(DefaultAppsTest, ConsecutiveSavesOverwritePreviousValue) {
  DefaultAppsConfig first;
  first.terminal_command = "foot";
  save_default_apps_config(first);

  DefaultAppsConfig second;
  second.terminal_command = "kitty";
  save_default_apps_config(second);

  EXPECT_EQ(load_default_apps_config().terminal_command, "kitty");
}

TEST_F(DefaultAppsTest, UserConfigPathEndsWithDefaultAppsToml) {
  EXPECT_NE(default_apps_user_config_path().find("default_apps.toml"), std::string::npos);
}

TEST_F(DefaultAppsTest, SystemDefaultConfigPathDiffersFromUserPath) {
  EXPECT_NE(default_apps_user_config_path(), default_apps_system_default_config_path());
}

TEST_F(DefaultAppsTest, LongCommandStringRoundTrips) {
  DefaultAppsConfig config;
  config.terminal_command =
      "some-terminal --config /home/user/.config/some-terminal/config.toml --class Terminal";
  save_default_apps_config(config);
  EXPECT_EQ(load_default_apps_config().terminal_command,
            "some-terminal --config /home/user/.config/some-terminal/config.toml --class "
            "Terminal");
}

TEST_F(DefaultAppsTest, CommandWithTabCharacterRoundTrips) {
  DefaultAppsConfig config;
  config.terminal_command = "term\t--flag";
  save_default_apps_config(config);
  EXPECT_EQ(load_default_apps_config().terminal_command, "term\t--flag");
}

TEST_F(DefaultAppsTest, WhitespaceOnlyCommandRoundTrips) {
  DefaultAppsConfig config;
  config.terminal_command = "   ";
  save_default_apps_config(config);
  EXPECT_EQ(load_default_apps_config().terminal_command, "   ");
}

TEST_F(DefaultAppsTest, CommandLooksLikeAbsolutePathRoundTrips) {
  DefaultAppsConfig config;
  config.terminal_command = "/usr/local/bin/foot --server";
  save_default_apps_config(config);
  EXPECT_EQ(load_default_apps_config().terminal_command, "/usr/local/bin/foot --server");
}

TEST_F(DefaultAppsTest, SingleCharacterCommandRoundTrips) {
  DefaultAppsConfig config;
  config.terminal_command = "x";
  save_default_apps_config(config);
  EXPECT_EQ(load_default_apps_config().terminal_command, "x");
}

TEST_F(DefaultAppsTest, CommandWithNewlineRoundTrips) {
  DefaultAppsConfig config;
  config.terminal_command = "term\n--flag";
  save_default_apps_config(config);
  EXPECT_EQ(load_default_apps_config().terminal_command, "term\n--flag");
}

TEST_F(DefaultAppsTest, ConfigFileWithExtraWhitespaceAroundKeyParses) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "default_apps.toml");
  out << "  terminal_command   =   \"kitty\"  \n";
  out.close();
  EXPECT_EQ(load_default_apps_config().terminal_command, "kitty");
}

TEST_F(DefaultAppsTest, CommandWithMultipleFlagsRoundTrips) {
  DefaultAppsConfig config;
  config.terminal_command = "foot -e tmux -w --hold";
  save_default_apps_config(config);
  EXPECT_EQ(load_default_apps_config().terminal_command, "foot -e tmux -w --hold");
}

TEST_F(DefaultAppsTest, DefaultTerminalCommandIsFoot) {
  EXPECT_EQ(DefaultAppsConfig{}.terminal_command, "foot");
}

}  // namespace
}  // namespace fleetwm
