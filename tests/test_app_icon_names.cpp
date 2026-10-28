#include <gtest/gtest.h>

#include <algorithm>

#include "app_icon.hpp"

namespace fleetwm::kit {
namespace {

bool names_have(const std::vector<std::string>& v, const char* s) { return std::find(v.begin(), v.end(), s) != v.end(); }

TEST(IconNames, WineExecutableSpellings) {
  const auto c = icon_name_candidates("Battle.net.exe");
  ASSERT_FALSE(c.empty());
  EXPECT_EQ(c.front(), "battle.net.exe");
  EXPECT_TRUE(names_have(c, "battle.net"));
  EXPECT_TRUE(names_have(c, "battle-net"));
  EXPECT_TRUE(names_have(c, "battlenet"));
}

TEST(IconNames, ReverseDnsIdsKeepTheirLastWords) {
  const auto c = icon_name_candidates("org.gnome.Calculator.desktop");
  EXPECT_TRUE(names_have(c, "org.gnome.calculator"));
  EXPECT_TRUE(names_have(c, "calculator"));
  EXPECT_TRUE(names_have(c, "gnome-calculator"));
  EXPECT_TRUE(names_have(icon_name_candidates("com.valvesoftware.Steam"), "steam"));
}

TEST(IconNames, PlainNamesStayAndNothingIsEmptyOrRepeated) {
  const auto c = icon_name_candidates("firefox");
  ASSERT_EQ(c.size(), 1u);
  EXPECT_EQ(c[0], "firefox");
  EXPECT_TRUE(icon_name_candidates("").empty());
  const auto d = icon_name_candidates("my game-bin");
  EXPECT_TRUE(names_have(d, "my-game"));
  for (size_t i = 0; i < d.size(); ++i)
    for (size_t j = i + 1; j < d.size(); ++j) EXPECT_NE(d[i], d[j]);
}

TEST(IconNames, WinesOwnProgramsHaveThemeNames) {
  EXPECT_EQ(wine_builtin_icon_names("notepad.exe").front(), "wine-notepad");
  EXPECT_EQ(wine_builtin_icon_names("Winecfg.exe").front(), "wine-winecfg");
  EXPECT_EQ(wine_builtin_icon_names("C:\\windows\\system32\\regedit.exe").front(), "wine-regedit");
  EXPECT_TRUE(wine_builtin_icon_names("battle.net.exe").empty());
}

}  // namespace
}  // namespace fleetwm::kit
