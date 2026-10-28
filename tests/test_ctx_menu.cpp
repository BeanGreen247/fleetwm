// The taskbar right-click menu: item strings, pinning in bar.toml, placement, size.
#include <gtest/gtest.h>

#include "ctx_menu.hpp"
#include "popup_namespaces.hpp"
#include "popup_spot.hpp"

using namespace fleetwm;

TEST(CtxMenu, ItemsRoundTrip) {
  for (const MenuItem& i : {MenuItem{MenuItem::Kind::Exec, "Task Manager", "fleetwm-taskmgr"}, MenuItem{MenuItem::Kind::Ipc, "Close window", "WINDOW_CLOSE 12"},
                            MenuItem{MenuItem::Kind::Pin, "Pin to taskbar", "firefox.desktop"}, MenuItem{MenuItem::Kind::Unpin, "Unpin", "firefox.desktop"},
                            MenuItem{MenuItem::Kind::Separator, "", ""}}) {
    const auto back = parse_menu_item(format_menu_item(i));
    ASSERT_TRUE(back.has_value()) << format_menu_item(i);
    EXPECT_EQ(*back, i);
  }
}

TEST(CtxMenu, MalformedItemsAreRejected) {
  EXPECT_FALSE(parse_menu_item("").has_value());
  EXPECT_FALSE(parse_menu_item("Label").has_value());
  EXPECT_FALSE(parse_menu_item("Label|exec").has_value());
  EXPECT_FALSE(parse_menu_item("Label|exec|").has_value());
  EXPECT_FALSE(parse_menu_item("Label|bogus|x").has_value());
  EXPECT_FALSE(parse_menu_item("|exec|x").has_value());
}

TEST(CtxMenu, ExecArgumentSplitsAtSpaces) {
  EXPECT_EQ(split_exec_arg("fleetwm-settings --page taskbar"), (std::vector<std::string>{"fleetwm-settings", "--page", "taskbar"}));
  EXPECT_TRUE(split_exec_arg("   ").empty());
}

TEST(CtxMenu, PinAndUnpinChangeTheListOnce) {
  BarConfig c;
  EXPECT_TRUE(apply_pin(&c, "a.desktop", true));
  EXPECT_FALSE(apply_pin(&c, "a.desktop", true));  // already there
  EXPECT_TRUE(apply_pin(&c, "b.desktop", true));
  EXPECT_EQ(c.pinned_apps, (std::vector<std::string>{"a.desktop", "b.desktop"}));
  EXPECT_TRUE(apply_pin(&c, "a.desktop", false));
  EXPECT_FALSE(apply_pin(&c, "a.desktop", false));
  EXPECT_EQ(c.pinned_apps, (std::vector<std::string>{"b.desktop"}));
  EXPECT_FALSE(apply_pin(&c, "", true));
}

TEST(CtxMenu, OpensOnTheDesktopSideOfEachEdge) {
  const MenuSpot b = ctx_menu_spot(TaskbarPosition::Bottom, 500, 200, 100, 1280, 720);
  EXPECT_TRUE(b.anchor & kAnchorBottom);
  EXPECT_EQ(b.bottom, kTaskbarThickness + 6);
  EXPECT_EQ(b.left, 400);  // centred on the click
  const MenuSpot t = ctx_menu_spot(TaskbarPosition::Top, 500, 200, 100, 1280, 720);
  EXPECT_TRUE(t.anchor & kAnchorTop);
  EXPECT_EQ(t.top, kTaskbarThickness + 6);
  const MenuSpot l = ctx_menu_spot(TaskbarPosition::Left, 300, 200, 100, 1280, 720);
  EXPECT_TRUE(l.anchor & kAnchorLeft);
  EXPECT_EQ(l.left, kTaskbarWidth + 6);
  EXPECT_EQ(l.top, 250);
  const MenuSpot r = ctx_menu_spot(TaskbarPosition::Right, 300, 200, 100, 1280, 720);
  EXPECT_TRUE(r.anchor & kAnchorRight);
  EXPECT_EQ(r.right, kTaskbarWidth + 6);
}

TEST(CtxMenu, StaysOnTheScreen) {
  EXPECT_EQ(ctx_menu_spot(TaskbarPosition::Bottom, 5, 200, 100, 1280, 720).left, 8);            // click at the far left
  EXPECT_EQ(ctx_menu_spot(TaskbarPosition::Bottom, 1275, 200, 100, 1280, 720).left, 1280 - 200 - 8);  // far right
  EXPECT_EQ(ctx_menu_spot(TaskbarPosition::Left, 715, 200, 100, 1280, 720).top, 720 - 100 - 8);
  EXPECT_EQ(ctx_menu_spot(TaskbarPosition::Left, 2, 200, 100, 1280, 720).top, 8);
  EXPECT_EQ(ctx_menu_spot(TaskbarPosition::Bottom, 500, 3000, 100, 1280, 720).left, 8);  // wider than the screen: pinned to the edge
}

TEST(CtxMenu, SizeGrowsWithRowsAndText) {
  std::vector<MenuItem> three(3, MenuItem{MenuItem::Kind::Exec, "x", "y"});
  std::vector<MenuItem> sep = three;
  sep.push_back(MenuItem{});
  const MenuSize a = ctx_menu_size(three, 40), b = ctx_menu_size(sep, 40), c = ctx_menu_size(three, 300);
  EXPECT_GT(b.h, a.h);
  EXPECT_LT(b.h - a.h, 30);  // a separator is thinner than a row
  EXPECT_GT(c.w, a.w);
  EXPECT_EQ(a.w, 160);  // never narrower than 160
}

TEST(CtxMenu, ClosesOnAPressOutside) { EXPECT_TRUE(dismisses_on_outside_click(kCtxMenuNamespace)); }

namespace fleetwm {
namespace {

TEST(CtxMenuTree, FlagsAndSubmenusRoundTrip) {
  const auto item = parse_menu_item("Online|exec,checked,disabled|true");
  ASSERT_TRUE(item.has_value());
  EXPECT_TRUE(item->checked);
  EXPECT_TRUE(item->disabled);
  EXPECT_EQ(item->kind, MenuItem::Kind::Exec);
  EXPECT_EQ(format_menu_item(*item), "Online|exec,checked,disabled|true");
  const auto sub = parse_menu_item("Friends|sub|3");
  ASSERT_TRUE(sub.has_value());
  EXPECT_EQ(sub->kind, MenuItem::Kind::Submenu);
  EXPECT_FALSE(parse_menu_item("Friends|sub|three").has_value());
  EXPECT_FALSE(parse_menu_item("X|exec,bogus|true").has_value());
}

TEST(CtxMenuTree, LevelsSkipTheContentsOfSubmenus) {
  // Open, Friends > {Online, Away, More > {A}}, -, Quit
  std::vector<MenuItem> flat;
  for (const char* t : {"Open|exec|a", "Friends|sub|4", "Online|exec|b", "Away|exec|c", "More|sub|1", "A|exec|d", "-", "Quit|exec|e"}) flat.push_back(*parse_menu_item(t));
  const auto top = menu_level(flat, 0, flat.size());
  ASSERT_EQ(top.size(), 4u);  // Open, Friends, separator, Quit
  EXPECT_EQ(top[1].item.label, "Friends");
  EXPECT_EQ(top[3].item.label, "Quit");
  const auto range = submenu_range(flat, top[1].index);
  const auto inner = menu_level(flat, range.first, range.second);
  ASSERT_EQ(inner.size(), 3u);  // Online, Away, More
  EXPECT_EQ(inner[2].item.label, "More");
  const auto deeper = submenu_range(flat, inner[2].index);
  EXPECT_EQ(menu_level(flat, deeper.first, deeper.second).size(), 1u);
  EXPECT_EQ(submenu_range(flat, 0), (std::pair<size_t, size_t>{0, 0}));  // not a submenu
}

TEST(CtxMenuTree, TheCardFitsTheTallestLevel) {
  std::vector<MenuItem> flat;
  for (const char* t : {"Friends|sub|3", "A|exec|a", "B|exec|b", "C|exec|c"}) flat.push_back(*parse_menu_item(t));
  // the submenu has a Back row and a rule above its three rows: taller than the top level's single row
  EXPECT_GT(menu_max_height(flat), ctx_menu_size({flat[0]}, 100).h);
}

}  // namespace
}  // namespace fleetwm
