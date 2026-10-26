#include <gtest/gtest.h>

#include <set>

#include "settings_catalog.hpp"
#include "test_util.hpp"

using namespace fleetwm;

TEST(SettingsCatalog, EveryPageBelongsToExactlyOneCategory) {
  const auto& pages = settings_pages();
  ASSERT_EQ(static_cast<int>(pages.size()), kPageCount);
  std::set<int> seen;
  for (size_t c = 0; c < settings_categories().size(); ++c)
    for (int p : settings_categories()[c].pages) {
      EXPECT_GT(p, 0);
      EXPECT_LT(p, kPageCount);
      EXPECT_TRUE(seen.insert(p).second) << "page " << p << " is in two categories";
      EXPECT_EQ(pages[static_cast<size_t>(p)].category, static_cast<int>(c)) << pages[static_cast<size_t>(p)].name;
    }
  EXPECT_EQ(static_cast<int>(seen.size()), kPageCount - 1);  // every page but Home
}

TEST(SettingsCatalog, EveryPageHasAnIconAndADescriptionAndTasksPointAtRealPages) {
  for (const SettingsPageInfo& p : settings_pages()) {
    EXPECT_NE(std::string(p.name), "");
    EXPECT_NE(std::string(p.description), "");
    EXPECT_NE(std::string(p.icon), "");
  }
  for (size_t c = 0; c < settings_categories().size(); ++c) {
    EXPECT_FALSE(settings_tasks(static_cast<int>(c)).empty());
    for (const SettingsTask& t : settings_tasks(static_cast<int>(c))) {
      EXPECT_GT(t.page, 0);
      EXPECT_LT(t.page, kPageCount);
    }
  }
  EXPECT_TRUE(settings_tasks(99).empty());
}

TEST(SettingsCatalog, SearchFindsBySettingKeywordAndPageName) {
  auto top = [](const char* q) { const auto h = search_settings(q); return h.empty() ? -1 : h[0].page; };
  EXPECT_EQ(top("wifi"), kPageNetwork);
  EXPECT_EQ(top("dark mode"), kPageTheme);
  EXPECT_EQ(top("resolution"), kPageDisplay);
  EXPECT_EQ(top("mouse"), kPageMouse);
  EXPECT_EQ(top("timezone"), kPageDateTime);
  EXPECT_EQ(top("vnc"), kPageRemote);
  EXPECT_EQ(top("  WIFI  "), kPageNetwork);  // case and spaces do not matter
}

TEST(SettingsCatalog, SearchNeedsEveryWordAndNothingMatchesNothing) {
  EXPECT_TRUE(search_settings("").empty());
  EXPECT_TRUE(search_settings("   ").empty());
  EXPECT_TRUE(search_settings("zzzzqq").empty());
  EXPECT_TRUE(search_settings("wifi zzzzqq").empty());
  EXPECT_FALSE(search_settings("remote frame").empty());
}

TEST(SettingsCatalog, SearchIsBestFirstBoundedAndWithoutDuplicates) {
  const auto h = search_settings("a", 5);
  EXPECT_LE(h.size(), 5u);
  for (size_t i = 1; i < h.size(); ++i) EXPECT_GE(h[i - 1].score, h[i].score);
  const auto m = search_settings("mouse");
  std::set<std::pair<int, std::string>> seen;
  for (const auto& x : m) EXPECT_TRUE(seen.insert({x.page, x.title}).second);
}

TEST(SettingsCatalog, RecentPagesAreMostRecentFirstWithoutHomeOrDuplicates) {
  std::vector<int> r;
  note_recent_page(r, kPageTheme);
  note_recent_page(r, kPageMouse);
  note_recent_page(r, kPageTheme);
  note_recent_page(r, kPageHome);
  note_recent_page(r, 999);
  EXPECT_EQ(r, (std::vector<int>{kPageTheme, kPageMouse}));
  for (int p : {kPageAudio, kPageDisplay, kPagePower}) note_recent_page(r, p);
  EXPECT_EQ(r.size(), 4u);
  EXPECT_EQ(r[0], kPagePower);
}

using SettingsStateTest = testutil::ScopedConfigHome;

TEST_F(SettingsStateTest, TheViewAndRecentPagesSurviveARestart) {
  setenv("XDG_STATE_HOME", (std::string(getenv("XDG_CONFIG_HOME") ? getenv("XDG_CONFIG_HOME") : "/tmp") + "/state").c_str(), 1);
  SettingsUiState s;
  s.view = HomeView::LargeIcons;
  s.recent = {kPageMouse, kPageTheme, kPageAudio};
  save_settings_ui_state(s);
  const SettingsUiState r = load_settings_ui_state();
  EXPECT_EQ(r.view, HomeView::LargeIcons);
  EXPECT_EQ(r.recent, s.recent);
  unsetenv("XDG_STATE_HOME");
}

TEST_F(SettingsStateTest, AMissingOrDamagedFileGivesTheDefaults) {
  setenv("XDG_STATE_HOME", (std::string(getenv("XDG_CONFIG_HOME") ? getenv("XDG_CONFIG_HOME") : "/tmp") + "/state-none").c_str(), 1);
  const SettingsUiState r = load_settings_ui_state();
  EXPECT_EQ(r.view, HomeView::Categories);
  EXPECT_TRUE(r.recent.empty());
  unsetenv("XDG_STATE_HOME");
}
