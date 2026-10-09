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
