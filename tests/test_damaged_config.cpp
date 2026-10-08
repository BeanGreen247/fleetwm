#include <gtest/gtest.h>

#include "bar_config.hpp"
#include "default_apps.hpp"
#include "keybinds_config.hpp"
#include "test_util.hpp"
#include "theme.hpp"
#include "wallpaper_config.hpp"

namespace fleetwm {
namespace {

using DamagedConfigTest = testutil::ScopedConfigHome;

// A typo in a user's config file used to throw out of the loader and end the compositor (and every helper) at start-up.
TEST_F(DamagedConfigTest, TypoInAnyConfigFileKeepsTheDefaults) {
  const std::string typo = "window_layout = desktop\n[titlebar\nheight = \n";
  for (const char* name : {"theme.toml", "bar.toml", "keybinds.toml", "default_apps.toml", "wallpaper.toml"}) write_config(name, typo);
  EXPECT_NO_THROW({
    EXPECT_EQ(load_theme_config().window_layout, ThemeConfig{}.window_layout);
    EXPECT_EQ(load_theme_config().titlebar.height, TitlebarConfig{}.height);
    EXPECT_EQ(load_bar_config().clock.use_24h, BarConfig{}.clock.use_24h);
    EXPECT_EQ(load_keybinds_config().terminal, KeybindsConfig{}.terminal);
    EXPECT_EQ(load_default_apps_config().terminal_command, DefaultAppsConfig{}.terminal_command);
    EXPECT_EQ(load_wallpaper_config().path, WallpaperConfig{}.path);
  });
}

}  // namespace
}  // namespace fleetwm
