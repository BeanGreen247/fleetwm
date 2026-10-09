#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

#include "desktop.hpp"
#include "test_util.hpp"

using namespace fleetwm::fm;
namespace fs = std::filesystem;

namespace {
class FmDesktop : public fleetwm::testutil::ScopedConfigHome {
 protected:
  void SetUp() override {
    ScopedConfigHome::SetUp();
    root_ = fs::temp_directory_path() / ("fm-desk-" + std::to_string(::getpid()) + "-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
    fs::create_directories(root_ / "Desktop/Projects");
    for (const char* n : {"b.txt", "a.txt", "photo.png", ".hidden"}) std::ofstream(root_ / "Desktop" / n) << "x";
    std::ofstream(root_ / "Desktop/app.desktop") << "[Desktop Entry]\nName=My App\nExec=true\n";
  }
  void TearDown() override {
    std::error_code ec;
    fs::remove_all(root_, ec);
    ScopedConfigHome::TearDown();
  }
  fs::path root_;
};
}  // namespace

TEST_F(FmDesktop, DefaultsAndRoundTrip) {
  DesktopConfig c = load_desktop_config();
  EXPECT_TRUE(c.show_icons);
  EXPECT_TRUE(c.auto_arrange);
  EXPECT_TRUE(c.show_hint);
  EXPECT_EQ(c.icon_size, DesktopIconSize::Medium);
  c.show_icons = false;
  c.icon_size = DesktopIconSize::Large;
  c.sort_key = SortKey::Modified;
  c.ascending = false;
  c.auto_arrange = false;
  c.align_to_grid = false;
  c.show_hint = false;
  c.show_trash = false;
  c.cells["a.txt"] = {3, 2};
  save_desktop_config(c);
  EXPECT_EQ(load_desktop_config(), c);
  write_config("desktop.toml", "icon_size = [[[");
  EXPECT_EQ(load_desktop_config(), DesktopConfig{});
  EXPECT_EQ(desktop_icon_px(DesktopIconSize::Small), 32);
  EXPECT_EQ(desktop_icon_px(DesktopIconSize::Large), 64);
}

TEST_F(FmDesktop, ListsTheSpecialIconsThenTheDesktopFolder) {
  const auto icons = list_desktop_icons((root_ / "Desktop").string(), (root_ / "home").string(), DesktopConfig{});
  ASSERT_GE(icons.size(), 8u);
  EXPECT_EQ(icons[0].key, "computer");
  EXPECT_EQ(icons[1].key, "home");
  EXPECT_EQ(icons[1].label, "home");
  EXPECT_EQ(icons[2].key, "trash");
  EXPECT_TRUE(icons[0].special);
  std::vector<std::string> names;
  for (size_t i = 3; i < icons.size(); ++i) names.push_back(icons[i].label);
  EXPECT_EQ(names, (std::vector<std::string>{"Projects", "a.txt", "My App", "b.txt", "photo.png"})) << "folders first, hidden files left out, launchers named by Name=";
}

TEST_F(FmDesktop, SpecialIconsCanBeHiddenAndTheOrderFollowsTheSort) {
  DesktopConfig c;
  c.show_computer = c.show_home = c.show_trash = false;
  c.sort_key = SortKey::Name;
  c.ascending = false;
  const auto icons = list_desktop_icons((root_ / "Desktop").string(), (root_ / "home").string(), c);
  ASSERT_FALSE(icons.empty());
  EXPECT_FALSE(icons[0].special);
  EXPECT_EQ(icons[0].label, "Projects") << "folders stay first";
  EXPECT_EQ(icons.back().label, "a.txt") << "names sort descending by file name: photo, b, app.desktop, a";
  EXPECT_EQ(icons[1].label, "photo.png");
}

TEST(FmDesktopGrid, CellsFillDownTheColumnsFromTheTopLeft) {
  const DesktopGrid g = desktop_grid(1024, 700, DesktopIconSize::Medium);
  EXPECT_GE(g.cols, 8);
  EXPECT_GE(g.rows, 5);
  std::vector<DesktopIcon> icons(g.rows + 2);
  for (size_t i = 0; i < icons.size(); ++i) icons[i].key = "k" + std::to_string(i);
  place_desktop_icons(&icons, g, DesktopConfig{});
  EXPECT_EQ(icons[0].col, 0);
  EXPECT_EQ(icons[0].row, 0);
  EXPECT_EQ(icons[1].row, 1);
  EXPECT_EQ(icons[g.rows].col, 1) << "the next column when the first is full";
  EXPECT_EQ(icons[g.rows].row, 0);
  const Pt o = desktop_cell_origin(g, 1, 2);
  EXPECT_EQ(o.x, g.margin_x + g.cell_w);
  EXPECT_EQ(o.y, g.margin_y + 2 * g.cell_h);
}

TEST(FmDesktopGrid, IconSizeChangesTheCells) {
  const DesktopGrid s = desktop_grid(1024, 768, DesktopIconSize::Small), l = desktop_grid(1024, 768, DesktopIconSize::Large);
  EXPECT_LT(s.cell_w, l.cell_w);
  EXPECT_GT(s.cols, l.cols);
  EXPECT_EQ(s.icon_px, 32);
}

TEST(FmDesktopGrid, ManualCellsAreKeptAndNewIconsTakeFreeOnes) {
  const DesktopGrid g = desktop_grid(1024, 768, DesktopIconSize::Medium);
  DesktopConfig c;
  c.auto_arrange = false;
  c.cells["a"] = {4, 1};
  c.cells["b"] = {0, 0};
  std::vector<DesktopIcon> icons(3);
  icons[0].key = "a";
  icons[1].key = "new";
  icons[2].key = "b";
  place_desktop_icons(&icons, g, c);
  EXPECT_EQ(icons[0].col, 4);
  EXPECT_EQ(icons[0].row, 1);
  EXPECT_EQ(icons[2].col, 0);
  EXPECT_EQ(icons[2].row, 0);
  EXPECT_EQ(icons[1].col, 0);
  EXPECT_EQ(icons[1].row, 1) << "the first free cell, not (0,0) which belongs to b";
}

TEST(FmDesktopGrid, ACellOutsideTheScreenFallsBackToAFreeOne) {
  const DesktopGrid g = desktop_grid(640, 480, DesktopIconSize::Medium);
  DesktopConfig c;
  c.auto_arrange = false;
  c.cells["far"] = {50, 50};
  std::vector<DesktopIcon> icons(1);
  icons[0].key = "far";
  place_desktop_icons(&icons, g, c);
  EXPECT_LT(icons[0].col, g.cols);
  EXPECT_LT(icons[0].row, g.rows);
}

TEST(FmDesktopGrid, HitTestingAndRubberBand) {
  const DesktopGrid g = desktop_grid(1024, 768, DesktopIconSize::Medium);
  std::vector<DesktopIcon> icons(4);
  for (size_t i = 0; i < icons.size(); ++i) icons[i].key = "k" + std::to_string(i);
  place_desktop_icons(&icons, g, DesktopConfig{});
  const Pt o = desktop_cell_origin(g, icons[1].col, icons[1].row);
  EXPECT_EQ(desktop_icon_at(icons, g, o.x + g.cell_w / 2, o.y + g.icon_px / 2 + 4), 1);
  EXPECT_EQ(desktop_icon_at(icons, g, o.x + g.cell_w / 2, o.y + g.cell_h - 6), 1) << "the label counts";
  EXPECT_EQ(desktop_icon_at(icons, g, 900, 700), -1);
  EXPECT_EQ(desktop_icon_at(icons, g, o.x + 1, o.y + 1), -1) << "the corner of the cell is desktop";
  EXPECT_EQ(desktop_icons_in_rect(icons, g, 0, 0, g.cell_w - 1, 2 * g.cell_h), (std::vector<int>{0, 1}));
  EXPECT_EQ(desktop_icons_in_rect(icons, g, 5 * g.cell_w, 0, 6 * g.cell_w, 10).size(), 0u);
}

TEST(FmDesktopGrid, DraggingAnIconToATakenCellIsRefusedAndAFreeOneIsRemembered) {
  const DesktopGrid g = desktop_grid(1024, 768, DesktopIconSize::Medium);
  std::vector<DesktopIcon> icons(2);
  icons[0].key = "a";
  icons[1].key = "b";
  DesktopConfig c;
  place_desktop_icons(&icons, g, c);
  EXPECT_FALSE(move_desktop_icon(icons, g, &c, "a", icons[1].col, icons[1].row));
  EXPECT_TRUE(move_desktop_icon(icons, g, &c, "a", 5, 2));
  EXPECT_EQ(c.cells["a"], (std::pair<int, int>{5, 2}));
  EXPECT_EQ(c.cells["b"], (std::pair<int, int>{icons[1].col, icons[1].row})) << "the others stay where they were";
  EXPECT_EQ(desktop_cell_at(g, desktop_cell_origin(g, 3, 1).x + 10, desktop_cell_origin(g, 3, 1).y + 10), (std::pair<int, int>{3, 1}));
  EXPECT_EQ(desktop_cell_at(g, -50, 99999), (std::pair<int, int>{0, g.rows - 1}));
}

TEST(FmDesktopHint, KeyComboSplitsIntoCaps) {
  EXPECT_EQ(split_key_combo("Super+/"), (std::vector<std::string>{"Super", "/"}));
  EXPECT_EQ(split_key_combo("Alt+Shift+Q"), (std::vector<std::string>{"Alt", "Shift", "Q"}));
  EXPECT_EQ(split_key_combo("Ctrl++"), (std::vector<std::string>{"Ctrl", "+"}));
  EXPECT_TRUE(split_key_combo("").empty());
}
