#include "default_apps.hpp"

#include <gtest/gtest.h>

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

}  // namespace
}  // namespace fleetwm
