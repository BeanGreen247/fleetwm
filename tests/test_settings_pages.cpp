#include <gtest/gtest.h>

#include "settings_pages.hpp"

using namespace fleetwm;

namespace {
const std::vector<std::string> kTabs = {"Theme", "Bar",   "Wallpaper", "Display",     "Power",
                                        "Date & Time", "Default Apps", "Audio", "Performance", "About"};
}

TEST(SettingsPages, FindsATabByName) {
  EXPECT_EQ(find_settings_page(kTabs, "power"), 4);
  EXPECT_EQ(find_settings_page(kTabs, "Theme"), 0);
  EXPECT_EQ(find_settings_page(kTabs, "about"), 9);
}

TEST(SettingsPages, IgnoresCase) {
  EXPECT_EQ(find_settings_page(kTabs, "POWER"), 4);
  EXPECT_EQ(find_settings_page(kTabs, "pOwEr"), 4);
}

TEST(SettingsPages, AcceptsAPrefix) {
  EXPECT_EQ(find_settings_page(kTabs, "date"), 5);
  EXPECT_EQ(find_settings_page(kTabs, "default"), 6);
  EXPECT_EQ(find_settings_page(kTabs, "perf"), 8);
}

TEST(SettingsPages, ExactMatchBeatsAPrefixMatch) {
  const std::vector<std::string> tabs = {"Power Options", "Power"};
  EXPECT_EQ(find_settings_page(tabs, "power"), 1);
}

TEST(SettingsPages, UnknownOrEmptyIsMinusOne) {
  EXPECT_EQ(find_settings_page(kTabs, "nonsense"), -1);
  EXPECT_EQ(find_settings_page(kTabs, ""), -1);
  EXPECT_EQ(find_settings_page({}, "power"), -1);
}
