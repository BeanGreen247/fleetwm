#pragma once

// What the Settings window knows about itself apart from drawing: the pages, the Control-Panel-style categories that group them, the task
// links shown on the Home page, a search index over the individual settings, and the little state the window keeps (recent pages, how the Home
// page is laid out). Pure data and file reading, so it is unit tested.

#include <string>
#include <vector>

namespace fleetwm {

// Page numbers are the tab numbers of fleetwm-settings (the `--page NAME` argument and the drawing switch use them).
enum SettingsPage {
  kPageHome = 0, kPageTheme, kPageTaskbar, kPageWallpaper, kPageDisplay, kPageNetwork, kPageBluetooth, kPageKeyboard, kPageMouse,
  kPagePower, kPageDateTime, kPageDefaultApps, kPageAudio, kPagePerformance, kPageFileManager, kPageDesktopIcons, kPageRemote,
  kPageAbout, kPageAccount, kPageCount
};

struct SettingsPageInfo {
  const char* name;         // the sidebar label and `--page` name
  const char* description;  // one line under the page title
  const char* icon;         // freedesktop icon name
  int category;             // index into settings_categories(), -1 for Home
};

struct SettingsCategory {
  const char* name;
  const char* blurb;
  const char* icon;
  unsigned colour;  // 0xRRGGBB, the tile behind the icon when the icon theme has none
  std::vector<int> pages;
};

struct SettingsTask {
  const char* text;  // "Change the screen resolution"
  int page;
};

const std::vector<SettingsPageInfo>& settings_pages();
const std::vector<SettingsCategory>& settings_categories();
// The two or three common jobs shown under a category's name on the Home page.
const std::vector<SettingsTask>& settings_tasks(int category);

struct SearchHit {
  std::string title;  // the setting
  int page = 0;
  int score = 0;
};
// Settings matching every word of `query` (case blind, in the setting's name, its keywords or its page's name), best first, at most `max`.
std::vector<SearchHit> search_settings(const std::string& query, size_t max = 12);

// ---- state kept between runs: ~/.local/state/fleetwm/settings-ui.txt ----
enum class HomeView { Categories, LargeIcons, SmallIcons };
struct SettingsUiState {
  HomeView view = HomeView::Categories;
  std::vector<int> recent;  // page numbers, most recent first
};
SettingsUiState load_settings_ui_state();
void save_settings_ui_state(const SettingsUiState& state);
// Puts `page` first in `recent` (no duplicates, never Home, at most `keep` entries).
void note_recent_page(std::vector<int>& recent, int page, size_t keep = 4);

}  // namespace fleetwm
