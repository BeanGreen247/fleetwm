#include <gtest/gtest.h>

#include <unistd.h>

#include <filesystem>
#include <fstream>

#include "browser.hpp"
#include "grouping.hpp"
#include "view_metrics.hpp"

using namespace fleetwm::fm;
namespace fs = std::filesystem;

namespace {
DirListing make_listing(const std::vector<std::pair<std::string, bool>>& names, uint64_t size = 100) {
  DirListing l;
  for (const auto& [n, dir] : names) {
    Entry e;
    e.name_off = static_cast<uint32_t>(l.arena.size());
    e.name_len = static_cast<uint32_t>(n.size());
    e.kind = dir ? Kind::Dir : Kind::File;
    e.hidden = n[0] == '.';
    e.size = size;
    l.arena += n;
    l.entries.push_back(e);
  }
  return l;
}
Browser make_browser() {
  Browser b;
  b.path = "/home/me";
  b.set_listing(make_listing({{"zeta.txt", false}, {"Alpha", true}, {"beta.txt", false}, {".hidden", false}, {"file10", false}, {"file2", false}}));
  return b;
}
}  // namespace

// ---- Browser ----
TEST(FmBrowser, HiddenFilesFollowTheSettingAndFoldersGoFirst) {
  Browser b = make_browser();
  ASSERT_EQ(b.shown.size(), 5u);
  EXPECT_EQ(b.name_at(0), "Alpha");
  EXPECT_EQ(b.name_at(1), "beta.txt");
  EXPECT_EQ(b.name_at(2), "file2");
  EXPECT_EQ(b.name_at(3), "file10");
  b.show_hidden = true;
  b.rebuild();
  EXPECT_EQ(b.shown.size(), 6u);
}

TEST(FmBrowser, FilterNarrowsByPartOfTheNameIgnoringCase) {
  Browser b = make_browser();
  b.filter = "ETA";
  b.rebuild();
  ASSERT_EQ(b.shown.size(), 2u);
  EXPECT_EQ(b.name_at(0), "beta.txt");
  EXPECT_EQ(b.name_at(1), "zeta.txt");
  b.filter = "nomatch";
  b.rebuild();
  EXPECT_TRUE(b.shown.empty());
}

TEST(FmBrowser, SortingKeepsTheSelection) {
  Browser b = make_browser();
  b.select_only(1);  // beta.txt
  b.sort_key = SortKey::Name;
  b.ascending = false;
  b.rebuild();
  ASSERT_EQ(b.selected().size(), 1u);
  EXPECT_EQ(b.name_at(b.selected()[0]), "beta.txt");
  EXPECT_EQ(b.name_at(b.focus), "beta.txt");
}

TEST(FmBrowser, ReloadKeepsSelectedNamesThatStillExist) {
  Browser b = make_browser();
  b.select_only(1);
  b.toggle(2);
  b.set_listing(make_listing({{"Alpha", true}, {"beta.txt", false}, {"new.txt", false}}));
  ASSERT_EQ(b.selected().size(), 1u);
  EXPECT_EQ(b.name_at(b.selected()[0]), "beta.txt");
}

TEST(FmBrowser, ClickCtrlClickAndShiftClick) {
  Browser b = make_browser();
  b.select_only(1);
  EXPECT_EQ(b.selected_count(), 1);
  b.toggle(3);
  EXPECT_EQ(b.selected(), (std::vector<int>{1, 3}));
  b.toggle(1);
  EXPECT_EQ(b.selected(), (std::vector<int>{3}));
  b.select_only(0);
  b.select_to(3);
  EXPECT_EQ(b.selected(), (std::vector<int>{0, 1, 2, 3}));
  b.select_to(1);
  EXPECT_EQ(b.selected(), (std::vector<int>{0, 1})) << "shift keeps the anchor";
  b.select_only(4);
  b.select_to(2);
  EXPECT_EQ(b.selected(), (std::vector<int>{2, 3, 4}));
}

TEST(FmBrowser, SelectAllInvertAndBytes) {
  Browser b = make_browser();
  b.select_all();
  EXPECT_EQ(b.selected_count(), 5);
  EXPECT_EQ(b.selected_bytes(), 400u) << "folders add nothing";
  b.invert_selection();
  EXPECT_EQ(b.selected_count(), 0);
  b.select_only(0);
  b.invert_selection();
  EXPECT_EQ(b.selected_count(), 4);
  b.clear_selection();
  EXPECT_EQ(b.selected_count(), 0);
}

TEST(FmBrowser, RubberBandAddsOrReplaces) {
  Browser b = make_browser();
  b.select_only(0);
  b.select_indices({2, 3}, true);
  EXPECT_EQ(b.selected(), (std::vector<int>{0, 2, 3}));
  b.select_indices({4, 99, -1}, false);
  EXPECT_EQ(b.selected(), (std::vector<int>{4}));
}

TEST(FmBrowser, TypeAheadSelectsAndWrapsAndExpires) {
  Browser b = make_browser();
  EXPECT_EQ(b.type_ahead("f", 10.0), 2);
  EXPECT_EQ(b.type_ahead("f", 10.3), 3) << "the same letter again cycles";
  EXPECT_EQ(b.type_ahead("f", 10.5), 2);
  EXPECT_EQ(b.type_ahead("z", 20.0), 4);
  EXPECT_EQ(b.type_ahead("e", 20.4), 4) << "'ze' still matches zeta.txt";
  Browser c = make_browser();
  EXPECT_EQ(c.type_ahead("b", 1.0), 1);
  EXPECT_EQ(c.type_ahead("x", 5.0), -1);
  EXPECT_EQ(c.selected_count(), 1) << "a miss leaves the selection";
}

TEST(FmBrowser, HistoryBackForwardAndUp) {
  Browser b;
  b.path = "/home/me";
  EXPECT_FALSE(b.can_back());
  b.remember_current();
  b.path = "/home/me/docs";
  b.remember_current();
  b.path = "/home/me/docs/x";
  EXPECT_TRUE(b.can_back());
  EXPECT_EQ(b.go_back(), "/home/me/docs");
  b.path = "/home/me/docs";
  EXPECT_TRUE(b.can_forward());
  EXPECT_EQ(b.go_back(), "/home/me");
  b.path = "/home/me";
  EXPECT_EQ(b.go_forward(), "/home/me/docs");
  b.path = "/home/me/docs";
  b.remember_current();
  EXPECT_FALSE(b.can_forward()) << "going somewhere new drops the forward list";
  EXPECT_EQ(b.go_back(), "/home/me/docs");
}

TEST(FmBrowser, UpGoesToTheParentThenThisPc) {
  Browser b;
  b.path = "/home/me/docs";
  EXPECT_EQ(b.up_address(), "/home/me");
  b.path = "/home";
  EXPECT_EQ(b.up_address(), "/");
  b.path = "/";
  EXPECT_EQ(b.up_address(), "computer:///");
  b.place = PlaceKind::Computer;
  EXPECT_FALSE(b.can_up());
  b.place = PlaceKind::Trash;
  EXPECT_EQ(b.up_address(), "computer:///");
  b.place = PlaceKind::Remote;
  b.uri = "smb://nas/media/films/old";
  EXPECT_EQ(b.up_address(), "smb://nas/media/films");
  b.uri = "smb://nas/";
  EXPECT_EQ(b.up_address(), "network:///");
}

TEST(FmBrowser, AddressesOfSpecialPlaces) {
  Browser b;
  b.place = PlaceKind::Computer;
  EXPECT_EQ(b.address(), "computer:///");
  b.place = PlaceKind::Trash;
  EXPECT_EQ(b.address(), "trash:///");
  b.place = PlaceKind::Remote;
  b.uri = "sftp://me@h/x";
  EXPECT_EQ(b.address(), "sftp://me@h/x");
  b.place = PlaceKind::Local;
  b.path = "/tmp";
  EXPECT_EQ(b.address(), "/tmp");
}

TEST(FmBrowser, PathAtJoinsFolderAndName) {
  Browser b = make_browser();
  EXPECT_EQ(b.path_at(0), "/home/me/Alpha");
  b.path = "/";
  EXPECT_EQ(b.path_at(0), "/Alpha");
}

TEST(FmBrowser, FocusIsClampedAfterTheListShrinks) {
  Browser b = make_browser();
  b.select_only(4);
  b.set_listing(make_listing({{"a", false}}));
  b.fix_focus();
  EXPECT_LE(b.focus, 0);
}

// ---- view metrics ----
TEST(FmMetrics, DetailsIsOneColumnOfRows) {
  const ViewMetrics m = compute_metrics(ViewMode::Details, 100, 800, 400, 16, 22, 13, 24);
  EXPECT_EQ(m.cols, 1);
  EXPECT_EQ(m.content_h, 2200);
  EXPECT_EQ(max_scroll_y(m), 2200 - (400 - 24));
  const ItemRect r = item_rect(m, 3, 0, 0);
  EXPECT_EQ(r.y, 24 + 3 * 22);
  EXPECT_EQ(r.h, 22);
  EXPECT_EQ(item_rect(m, 3, 0, 40).y, 24 + 3 * 22 - 40);
}

TEST(FmMetrics, OnlyVisibleItemsAreInTheRange) {
  const ViewMetrics m = compute_metrics(ViewMode::Details, 100000, 800, 400, 16, 22, 13, 24);
  int a, b;
  visible_range(m, 0, 0, &a, &b);
  EXPECT_EQ(a, 0);
  EXPECT_LE(b, 20);
  visible_range(m, 0, 22 * 5000, &a, &b);
  EXPECT_EQ(a, 5000);
  EXPECT_LE(b - a, 20);
  const ViewMetrics none = compute_metrics(ViewMode::Details, 0, 800, 400, 16, 22, 13, 24);
  visible_range(none, 0, 0, &a, &b);
  EXPECT_GT(a, b);
}

TEST(FmMetrics, HitTestingMatchesTheRects) {
  for (ViewMode mode : {ViewMode::Details, ViewMode::List, ViewMode::SmallIcons, ViewMode::MediumIcons, ViewMode::LargeIcons, ViewMode::ExtraLargeIcons, ViewMode::Tiles, ViewMode::Content}) {
    const ViewMetrics m = compute_metrics(mode, 57, 800, 500, view_mode_icon_px(mode), 22, 13, mode == ViewMode::Details ? 24 : 0);
    for (int i : {0, 1, 7, 33, 56}) {
      const ItemRect r = item_rect(m, i, 0, 0);
      if (r.y + r.h > m.view_h || r.x + r.w > m.view_w + m.content_w) continue;
      EXPECT_EQ(index_at(m, r.x + r.w / 2, r.y + r.h / 2, 0, 0), i) << view_mode_key(mode) << " item " << i;
    }
    EXPECT_EQ(index_at(m, -5, 100, 0, 0), -1);
  }
}

TEST(FmMetrics, HitTestingPastTheLastItemAndOnTheHeaderIsNothing) {
  const ViewMetrics m = compute_metrics(ViewMode::Details, 3, 800, 400, 16, 22, 13, 24);
  EXPECT_EQ(index_at(m, 10, 10, 0, 0), -1) << "the header";
  EXPECT_EQ(index_at(m, 10, 24 + 3 * 22 + 5, 0, 0), -1) << "empty space below the last row";
  EXPECT_EQ(index_at(m, 10, 24 + 5, 0, 0), 0);
}

TEST(FmMetrics, IconGridsFillAcrossThenDown) {
  const ViewMetrics m = compute_metrics(ViewMode::MediumIcons, 20, 800, 500, 48, 22, 13);
  EXPECT_GT(m.cols, 4);
  const ItemRect r0 = item_rect(m, 0, 0, 0), r1 = item_rect(m, 1, 0, 0), rc = item_rect(m, m.cols, 0, 0);
  EXPECT_EQ(r0.y, r1.y);
  EXPECT_GT(r1.x, r0.x);
  EXPECT_EQ(rc.x, r0.x);
  EXPECT_GT(rc.y, r0.y);
}

TEST(FmMetrics, ListFillsDownThenAcrossAndScrollsSideways) {
  const ViewMetrics m = compute_metrics(ViewMode::List, 200, 800, 300, 16, 22, 13);
  EXPECT_TRUE(m.horizontal);
  EXPECT_EQ(m.rows, 300 / 22);
  const ItemRect a = item_rect(m, 0, 0, 0), b = item_rect(m, 1, 0, 0), c = item_rect(m, m.rows, 0, 0);
  EXPECT_EQ(a.x, b.x);
  EXPECT_GT(b.y, a.y);
  EXPECT_GT(c.x, a.x);
  EXPECT_EQ(max_scroll_y(m), 0);
  EXPECT_GT(max_scroll_x(m), 0);
}

TEST(FmMetrics, RubberBandTouchesOnlyWhatItCrosses) {
  const ViewMetrics m = compute_metrics(ViewMode::Details, 50, 800, 400, 16, 22, 13, 24);
  auto idx = indices_in_rect(m, 5, 24 + 22, 100, 24 + 22 * 3 + 1, 0, 0);
  EXPECT_EQ(idx, (std::vector<int>{1, 2, 3}));
  idx = indices_in_rect(m, 100, 24 + 22 * 3 + 1, 5, 24 + 22, 0, 0);
  EXPECT_EQ(idx, (std::vector<int>{1, 2, 3})) << "dragging up and to the left works the same";
  EXPECT_TRUE(indices_in_rect(m, 500, 30, 600, 100, 0, 0).empty()) << "right of the name area in details view";
}

TEST(FmMetrics, ScrollToShowBringsTheItemIntoView) {
  const ViewMetrics m = compute_metrics(ViewMode::Details, 100, 800, 224, 16, 22, 13, 24);
  int sx = 0, sy = 0;
  scroll_to_show(m, 50, &sx, &sy);
  const ItemRect r = item_rect(m, 50, sx, sy);
  EXPECT_GE(r.y, m.header_h);
  EXPECT_LE(r.y + r.h, m.view_h);
  scroll_to_show(m, 0, &sx, &sy);
  EXPECT_EQ(sy, 0);
  scroll_to_show(m, 99, &sx, &sy);
  EXPECT_EQ(sy, max_scroll_y(m));
  int ox = 0, oy = 7;
  scroll_to_show(m, -1, &ox, &oy);
  EXPECT_EQ(oy, 7);
}

TEST(FmMetrics, ArrowKeysFollowTheLayout) {
  const ViewMetrics d = compute_metrics(ViewMode::Details, 10, 800, 400, 16, 22, 13, 24);
  EXPECT_EQ(navigate(d, 4, Nav::Down), 5);
  EXPECT_EQ(navigate(d, 4, Nav::Up), 3);
  EXPECT_EQ(navigate(d, 0, Nav::Up), 0);
  EXPECT_EQ(navigate(d, 9, Nav::Down), 9);
  EXPECT_EQ(navigate(d, 4, Nav::Left), 4);
  EXPECT_EQ(navigate(d, 4, Nav::Home), 0);
  EXPECT_EQ(navigate(d, 4, Nav::End), 9);
  EXPECT_EQ(navigate(d, -1, Nav::Down), 0);
  EXPECT_EQ(navigate(d, -1, Nav::Up), 9);
  EXPECT_EQ(navigate(d, 0, Nav::PageDown), 9) << "a page is clamped to the last item";
  const ViewMetrics g = compute_metrics(ViewMode::MediumIcons, 30, 800, 500, 48, 22, 13);
  EXPECT_EQ(navigate(g, 5, Nav::Right), 6);
  EXPECT_EQ(navigate(g, 5, Nav::Left), 4);
  EXPECT_EQ(navigate(g, 5, Nav::Down), 5 + g.cols);
  EXPECT_EQ(navigate(g, 5 + g.cols, Nav::Up), 5);
  EXPECT_EQ(navigate(g, 0, Nav::Left), 0);
  EXPECT_EQ(navigate(g, g.cols - 1, Nav::Right), g.cols - 1) << "no wrap to the next row";
  const ViewMetrics l = compute_metrics(ViewMode::List, 100, 800, 300, 16, 22, 13);
  EXPECT_EQ(navigate(l, 0, Nav::Down), 1);
  EXPECT_EQ(navigate(l, 0, Nav::Right), l.rows);
  EXPECT_EQ(navigate(l, l.rows - 1, Nav::Down), l.rows - 1) << "bottom of a column stays";
  EXPECT_EQ(navigate(compute_metrics(ViewMode::Details, 0, 800, 400, 16, 22, 13), 0, Nav::Down), -1);
}

TEST(FmMetrics, ARaggedLastRowStillAllowsDown) {
  const ViewMetrics g = compute_metrics(ViewMode::MediumIcons, 30, 800, 500, 48, 22, 13);
  const int last_row_first = (29 / g.cols) * g.cols;
  const int above = last_row_first - g.cols + (29 % g.cols) + 1;  // a cell above the gap
  EXPECT_LE(navigate(g, above, Nav::Down), 29);
}

TEST(FmBrowser, SearchAndRecentResultsShowTheLastComponentAndKeepTheirFolder) {
  Browser b;
  b.path = "/home/me";
  b.set_listing(make_listing({{"docs/report.txt", false}, {"/var/log/syslog", false}, {"plain.txt", false}}));
  for (int i = 0; i < static_cast<int>(b.shown.size()); ++i) {
    const std::string n = b.name_at(i);
    if (n == "docs/report.txt") {
      EXPECT_EQ(b.label_at(i), "report.txt");
      EXPECT_EQ(b.folder_at(i), "docs");
      EXPECT_EQ(b.path_at(i), "/home/me/docs/report.txt");
    } else if (n == "/var/log/syslog") {
      EXPECT_EQ(b.label_at(i), "syslog");
      EXPECT_EQ(b.folder_at(i), "/var/log");
      EXPECT_EQ(b.path_at(i), "/var/log/syslog");
    } else {
      EXPECT_EQ(b.label_at(i), "plain.txt");
      EXPECT_EQ(b.folder_at(i), "");
    }
  }
}

TEST(FmBrowser, LazyEntriesGetTheirStatWhenAskedAndTheSourceListingToo) {
  const fs::path dir = fs::temp_directory_path() / ("fm-lazy-" + std::to_string(::getpid()));
  fs::create_directories(dir);
  std::ofstream(dir / "a.txt") << "12345";
  std::ofstream(dir / "b.txt") << "1234567890";
  Browser b;
  b.path = dir.string();
  DirListing l;
  ASSERT_TRUE(list_dir(b.path, {false, true}, &l));
  b.stat_complete = false;
  b.set_listing(std::move(l));
  ASSERT_EQ(b.shown.size(), 2u);
  EXPECT_FALSE(b.shown[0].has_stat);
  bool complete = true;
  b.select_all();
  b.selected_bytes(&complete);
  EXPECT_FALSE(complete);
  b.ensure_stat(0);
  EXPECT_TRUE(b.shown[0].has_stat);
  EXPECT_EQ(b.shown[0].size, 5u);
  EXPECT_TRUE(b.raw.entries[b.shown[0].src].has_stat) << "a later rebuild keeps it";
  b.ensure_stat_range(0, 5);
  EXPECT_EQ(b.selected_bytes(&complete), 15u);
  EXPECT_TRUE(complete);
  b.rebuild();
  EXPECT_TRUE(b.shown[0].has_stat);
  fs::remove_all(dir);
}

// ---- grouping in the icon and tile views ----
namespace {
// 7 items in 3 groups (0-2, 3-4, 5-6) in a view 4 cells wide: rows of 4 and 3, then 2, then 2.
ViewMetrics grouped_grid(ViewMode mode = ViewMode::MediumIcons, int view_h = 400) {
  const std::vector<int> groups = {0, 3, 5};
  ViewMetrics probe = compute_metrics(mode, 7, 800, view_h, 48, 22, 13);
  const int w = probe.cell_w * 4;  // exactly four cells across
  return compute_metrics(mode, 7, w, view_h, 48, 22, 13, 0, 0, &groups);
}
}  // namespace

TEST(FmViewMetrics, GroupedIconsStackGroupsWithHeadings) {
  const ViewMetrics m = grouped_grid();
  ASSERT_TRUE(m.grid_grouped());
  ASSERT_EQ(m.cols, 4);
  ASSERT_EQ(m.group_head_y.size(), 3u);
  EXPECT_EQ(m.group_head_y[0], 0);
  EXPECT_EQ(m.group_items_y[0], m.group_head_h);
  // group 0 has 3 items: one row. Group 1 starts below it.
  EXPECT_EQ(m.group_head_y[1], m.group_head_h + m.cell_h);
  EXPECT_EQ(m.group_head_y[2], 2 * (m.group_head_h + m.cell_h));
  EXPECT_EQ(m.content_h, 3 * (m.group_head_h + m.cell_h));
  // Items sit below their own heading, column by column.
  const ItemRect a = item_rect(m, 0, 0, 0), b = item_rect(m, 1, 0, 0), c = item_rect(m, 3, 0, 0);
  EXPECT_EQ(a.y, m.group_head_h);
  EXPECT_EQ(b.x, m.cell_w);
  EXPECT_EQ(b.y, a.y);
  EXPECT_EQ(c.x, 0) << "the first item of a group starts a new row at the left";
  EXPECT_EQ(c.y, m.group_items_y[1]);
  const ItemRect h = group_header_rect(m, 1, 0, 0);
  EXPECT_EQ(h.y, m.group_head_y[1]);
  EXPECT_EQ(h.w, m.view_w);
}

TEST(FmViewMetrics, GroupedGridHitTestingSkipsHeadingsAndEmptyCells) {
  const ViewMetrics m = grouped_grid();
  EXPECT_EQ(index_at(m, 5, 5, 0, 0), -1) << "a heading is not an item";
  EXPECT_EQ(index_at(m, 5, m.group_items_y[0] + 5, 0, 0), 0);
  EXPECT_EQ(index_at(m, m.cell_w * 2 + 5, m.group_items_y[0] + 5, 0, 0), 2);
  EXPECT_EQ(index_at(m, m.cell_w * 3 + 5, m.group_items_y[0] + 5, 0, 0), -1) << "the fourth cell of a three-item row is empty";
  EXPECT_EQ(index_at(m, 5, m.group_items_y[2] + 5, 0, 0), 5);
  EXPECT_EQ(index_at(m, m.cell_w + 5, m.group_items_y[2] + 5, 0, 0), 6);
  EXPECT_EQ(index_at(m, 5, m.content_h + 50, 0, 0), -1);
}

TEST(FmViewMetrics, GroupedGridVisibleRangeFollowsTheScroll) {
  const ViewMetrics m = grouped_grid(ViewMode::MediumIcons, 100);  // shorter than the content
  int first, last;
  visible_range(m, 0, 0, &first, &last);
  EXPECT_EQ(first, 0);
  EXPECT_GE(last, 2);
  visible_range(m, 0, m.group_head_y[2], &first, &last);
  EXPECT_EQ(first, 5);
  EXPECT_EQ(last, 6);
  // Items that are visible are exactly those whose rectangles touch the view.
  for (int sy : {0, 40, 130, 200, max_scroll_y(m)}) {
    visible_range(m, 0, sy, &first, &last);
    for (int i = 0; i < m.count; ++i) {
      const ItemRect r = item_rect(m, i, 0, sy);
      const bool touches = r.y < m.view_h && r.y + r.h > 0;
      if (touches) EXPECT_TRUE(i >= first && i <= last) << "item " << i << " at scroll " << sy;
    }
  }
}

TEST(FmViewMetrics, GroupedGridArrowKeysCrossGroups) {
  const ViewMetrics m = grouped_grid();
  EXPECT_EQ(navigate(m, 1, Nav::Down), 4) << "from group 0, column 1: the group below, same column";
  EXPECT_EQ(navigate(m, 2, Nav::Down), 4) << "column 2 does not exist in the two-item group: its last cell";
  EXPECT_EQ(navigate(m, 4, Nav::Up), 1);
  EXPECT_EQ(navigate(m, 3, Nav::Down), 5);
  EXPECT_EQ(navigate(m, 6, Nav::Down), 6) << "the last row stays put";
  EXPECT_EQ(navigate(m, 0, Nav::Up), 0);
  EXPECT_EQ(navigate(m, 2, Nav::Right), 2) << "right stops at the end of a group's row";
  EXPECT_EQ(navigate(m, 3, Nav::Left), 3) << "left stops at the first column";
  EXPECT_EQ(navigate(m, 3, Nav::Right), 4);
  EXPECT_EQ(navigate(m, 0, Nav::End), 6);
}

TEST(FmViewMetrics, GroupedGridScrollShowsTheHeadingWithTheFirstRow) {
  const ViewMetrics m = grouped_grid(ViewMode::MediumIcons, 200);
  int sx = 0, sy = 0;
  scroll_to_show(m, 5, &sx, &sy);
  EXPECT_LE(sy, m.group_head_y[2]) << "scrolling to a group's first item brings its heading into view";
  EXPECT_GT(sy, 0);
}

TEST(FmViewMetrics, GroupingAlsoWorksInTilesAndLeavesListAndContentAlone) {
  const std::vector<int> groups = {0, 3, 5};
  EXPECT_TRUE(compute_metrics(ViewMode::Tiles, 7, 800, 400, 48, 22, 13, 0, 0, &groups).grid_grouped());
  EXPECT_FALSE(compute_metrics(ViewMode::List, 7, 800, 400, 16, 22, 13, 0, 0, &groups).grid_grouped());
  EXPECT_TRUE(compute_metrics(ViewMode::List, 7, 800, 400, 16, 22, 13, 0, 0, &groups).group_starts.empty());
  EXPECT_FALSE(compute_metrics(ViewMode::Content, 7, 800, 400, 48, 22, 13, 0, 0, &groups).grid_grouped());
  EXPECT_FALSE(compute_metrics(ViewMode::MediumIcons, 7, 800, 400, 48, 22, 13).grid_grouped());
}

// ---- grouping ----
TEST(FmMetrics, GroupedDetailsRowsHaveHeadings) {
  const std::vector<int> groups = {0, 3, 4};  // items 0-2, 3, 4-5
  const ViewMetrics m = compute_metrics(ViewMode::Details, 6, 800, 400, 16, 22, 13, 24, 0, &groups);
  EXPECT_EQ(m.rows, 9);
  EXPECT_EQ(row_of_item(m, 0), 1);
  EXPECT_EQ(row_of_item(m, 2), 3);
  EXPECT_EQ(row_of_item(m, 3), 5);
  EXPECT_EQ(row_of_item(m, 4), 7);
  bool header;
  int idx;
  row_info(m, 0, &header, &idx);
  EXPECT_TRUE(header);
  EXPECT_EQ(idx, 0);
  row_info(m, 4, &header, &idx);
  EXPECT_TRUE(header);
  EXPECT_EQ(idx, 1);
  row_info(m, 5, &header, &idx);
  EXPECT_FALSE(header);
  EXPECT_EQ(idx, 3);
  EXPECT_EQ(item_rect(m, 3, 0, 0).y, 24 + 5 * 22);
  EXPECT_EQ(group_header_rect(m, 1, 0, 0).y, 24 + 4 * 22);
  EXPECT_EQ(index_at(m, 10, 24 + 5, 0, 0), -1) << "a heading is not an item";
  EXPECT_EQ(index_at(m, 10, 24 + 22 + 5, 0, 0), 0);
  EXPECT_EQ(index_at(m, 10, 24 + 5 * 22 + 5, 0, 0), 3);
  EXPECT_EQ(m.content_h, 9 * 22);
}

TEST(FmMetrics, GroupedVisibleRangeCoversWhatIsOnScreen) {
  std::vector<int> groups;
  for (int g = 0; g < 100; ++g) groups.push_back(g * 10);
  const ViewMetrics m = compute_metrics(ViewMode::Details, 1000, 800, 224, 16, 22, 13, 24, 0, &groups);
  int a, b;
  visible_range(m, 0, 22 * 55, &a, &b);
  EXPECT_LE(a, 50);
  EXPECT_GE(a, 40);
  EXPECT_GT(b, a);
  int sx = 0, sy = 0;
  scroll_to_show(m, 500, &sx, &sy);
  const ItemRect r = item_rect(m, 500, sx, sy);
  EXPECT_GE(r.y, 24);
  EXPECT_LE(r.y + r.h, 224);
}

TEST(FmBrowser, GroupingOrdersGroupsAndKeepsTheSortInside) {
  Browser b;
  b.path = "/p";
  b.group_by = GroupBy::Name;
  b.set_listing(make_listing({{"banana", false}, {"Apple", false}, {"avocado", false}, {"7zip", false}, {"zeta", false}, {"_x", false}}));
  ASSERT_EQ(b.group_starts.size(), 5u);
  EXPECT_EQ(b.group_labels, (std::vector<std::string>{"0-9", "A", "B", "Z", "#"}));
  EXPECT_EQ(b.group_starts, (std::vector<int>{0, 1, 3, 4, 5}));
  EXPECT_EQ(b.name_at(1), "Apple");
  EXPECT_EQ(b.name_at(2), "avocado");
  b.ascending = false;
  b.rebuild();
  EXPECT_EQ(b.group_labels.front(), "#") << "the direction flips the groups";
}

TEST(FmBrowser, GroupingBySizeAndType) {
  Browser b;
  b.path = "/p";
  b.type_label = [](std::string_view n, bool d) { return d ? std::string("File folder") : (extension_of(n) == "txt" ? std::string("Text Document") : std::string("File")); };
  DirListing l = make_listing({{"a.txt", false}, {"b.bin", false}, {"dir", true}}, 100);
  l.entries[1].size = 5u << 20;
  b.group_by = GroupBy::Size;
  b.set_listing(std::move(l));
  ASSERT_EQ(b.group_labels.size(), 3u);
  EXPECT_EQ(b.group_labels[0], "Folders");
  EXPECT_EQ(b.group_labels[1], "Tiny (0 - 16 KB)");
  EXPECT_EQ(b.group_labels[2], "Medium (1 - 128 MB)");
  b.group_by = GroupBy::Type;
  b.rebuild();
  EXPECT_EQ(b.group_labels[0], "File folder");
  EXPECT_EQ(b.group_labels.size(), 3u);
}

TEST(FmGrouping, DateBucketsFollowTheCalendar) {
  setenv("TZ", "UTC", 1);
  tzset();
  const time_t now = 1791519544;  // Fri 2026-10-09 04:19 UTC
  auto lab = [&](int64_t t) { return group_of(GroupBy::Modified, "x", false, 0, t, now, "").label; };
  EXPECT_EQ(lab(now - 60), "Today");
  EXPECT_EQ(lab(now - 5 * 3600), "Yesterday");
  EXPECT_EQ(lab(now - 3 * 86400), "Earlier this week");
  EXPECT_EQ(lab(now - 8 * 86400), "Last week");
  EXPECT_EQ(lab(now - 20 * 86400), "Last month") << "Sep 19 is last month";
  EXPECT_EQ(lab(now - 100 * 86400), "Earlier this year");
  EXPECT_EQ(lab(now - 400 * 86400), "A long time ago");
}
