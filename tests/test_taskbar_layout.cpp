// Taskbar element order, hiding and the shared one-dimensional layout.
#include <gtest/gtest.h>

#include <algorithm>

#include "taskbar_layout.hpp"

using namespace fleetwm;

namespace {
double pos_of(const std::vector<TbSlot>& s, TbElement e) {
  for (const TbSlot& i : s)
    if (i.id == e) return i.pos;
  return -1;
}
double size_of(const std::vector<TbSlot>& s, TbElement e) {
  for (const TbSlot& i : s)
    if (i.id == e) return i.size;
  return -1;
}
bool has(const std::vector<TbSlot>& s, TbElement e) { return pos_of(s, e) >= 0; }
}  // namespace

TEST(TaskbarElements, NamesRoundTrip) {
  for (TbElement e : tb_default_order()) {
    TbElement back;
    ASSERT_TRUE(tb_element_from_name(tb_element_name(e), &back));
    EXPECT_EQ(back, e);
    EXPECT_GT(std::string(tb_element_label(e)).size(), 2u);
  }
  TbElement x;
  EXPECT_FALSE(tb_element_from_name("nonsense", &x));
  EXPECT_EQ(tb_default_order().size(), static_cast<size_t>(kTbElementCount));
}

TEST(TaskbarElements, IdlePinnedAppsStayIconOnly) {
  EXPECT_FALSE(tb_pinned_label_visible(true, false, false));
  EXPECT_TRUE(tb_pinned_label_visible(true, false, true));
  EXPECT_FALSE(tb_pinned_label_visible(false, false, true));
  EXPECT_FALSE(tb_pinned_label_visible(true, true, true));
}

TEST(TaskbarElements, NormalizeDropsUnknownAndRepeats) {
  const auto o = tb_normalize_order({"clock", "bogus", "start", "clock", "volume"});
  EXPECT_EQ(o.size(), static_cast<size_t>(kTbElementCount));
  EXPECT_EQ(o[0], TbElement::Clock);
  EXPECT_EQ(std::count(o.begin(), o.end(), TbElement::Clock), 1);
  EXPECT_EQ(o[1], TbElement::Start);
}

TEST(TaskbarElements, OlderFileGetsNewElementsNextToTheirNeighbours) {
  // A file written before Bluetooth existed: the new element lands right after the network icon.
  std::vector<std::string> old = {"start", "workspaces", "pinned", "windows", "metrics", "tray", "layout", "volume", "network", "mode", "battery", "clock"};
  const auto o = tb_normalize_order(old);
  auto net = std::find(o.begin(), o.end(), TbElement::Network);
  ASSERT_NE(net, o.end());
  EXPECT_EQ(*(net + 1), TbElement::Bluetooth);
  EXPECT_EQ(o.size(), static_cast<size_t>(kTbElementCount));
}

TEST(TaskbarElements, EmptyFileGivesTheDefault) { EXPECT_EQ(tb_normalize_order({}), tb_default_order()); }

TEST(TaskbarElements, HiddenListIsCleaned) {
  const auto h = tb_normalize_hidden({"metrics", "metrics", "zzz", "clock"});
  ASSERT_EQ(h.size(), 2u);
  EXPECT_EQ(h[0], TbElement::Metrics);
  EXPECT_EQ(h[1], TbElement::Clock);
}

TEST(TaskbarElements, MoveStaysInsideTheZone) {
  auto order = tb_default_order();
  // Pinned (main zone) moves up past Workspaces; cannot move above Start.
  EXPECT_TRUE(tb_move(&order, TbElement::Pinned, -1));
  EXPECT_LT(std::find(order.begin(), order.end(), TbElement::Pinned), std::find(order.begin(), order.end(), TbElement::Workspaces));
  EXPECT_TRUE(tb_move(&order, TbElement::Pinned, -1));
  EXPECT_FALSE(tb_move(&order, TbElement::Pinned, -1));
  // Windows (last of the main zone) cannot go down into the status zone.
  EXPECT_FALSE(tb_move(&order, TbElement::Windows, +1));
  // Tray (first status element after Metrics): moving up swaps with Metrics, not with Windows.
  EXPECT_TRUE(tb_move(&order, TbElement::Tray, -1));
  EXPECT_FALSE(tb_move(&order, TbElement::Tray, -1));
  EXPECT_TRUE(tb_is_main_zone(*(std::find(order.begin(), order.end(), TbElement::Tray) - 1)));
  // The clock can move up through the status zone.
  EXPECT_TRUE(tb_move(&order, TbElement::Clock, -1));
  EXPECT_TRUE(tb_move(&order, TbElement::Clock, +1));
}

TEST(TaskbarElements, DefaultOrderPlacesStatusFromTheEnd) {
  std::vector<TbItem> items = {{TbElement::Start, 46}, {TbElement::Workspaces, 100}, {TbElement::Windows, 500}, {TbElement::Tray, 40}, {TbElement::Clock, 60}};
  TbLayoutParams p;
  p.length = 1000;
  const auto s = tb_layout(items, p);
  EXPECT_DOUBLE_EQ(pos_of(s, TbElement::Start), 6);
  EXPECT_DOUBLE_EQ(pos_of(s, TbElement::Clock), 1000 - 6 - 60);
  EXPECT_DOUBLE_EQ(pos_of(s, TbElement::Tray), 1000 - 6 - 60 - 10 - 40);
  // The window list fills what is left, ending one gap before the tray.
  const double w_end = pos_of(s, TbElement::Windows) + size_of(s, TbElement::Windows);
  EXPECT_DOUBLE_EQ(w_end + 10, pos_of(s, TbElement::Tray));
  EXPECT_DOUBLE_EQ(pos_of(s, TbElement::Windows), 6 + 46 + 10 + 100 + 10);
}

TEST(TaskbarElements, ReorderedMainZoneKeepsTheListLast) {
  std::vector<TbItem> items = {{TbElement::Windows, 500}, {TbElement::Start, 46}, {TbElement::Clock, 60}};
  TbLayoutParams p;
  p.length = 800;
  const auto s = tb_layout(items, p);
  // Windows first in the user's order: it sits first and takes the free room, Start follows it.
  EXPECT_LT(pos_of(s, TbElement::Windows), pos_of(s, TbElement::Start));
  EXPECT_LT(pos_of(s, TbElement::Start), pos_of(s, TbElement::Clock));
  EXPECT_LE(pos_of(s, TbElement::Start) + 46, pos_of(s, TbElement::Clock));
}

TEST(TaskbarElements, WindowListDroppedWhenTooNarrow) {
  std::vector<TbItem> items = {{TbElement::Start, 46}, {TbElement::Windows, 400}, {TbElement::Clock, 60}};
  TbLayoutParams p;
  p.length = 140;  // 6 + 46 + 10 + [x] + 10 + 60 + 6 leaves 2
  const auto s = tb_layout(items, p);
  EXPECT_FALSE(has(s, TbElement::Windows));
  EXPECT_TRUE(has(s, TbElement::Start));
}

TEST(TaskbarElements, CompactWindowListUsesRequestedWidth) {
  std::vector<TbItem> items = {{TbElement::Start, 46}, {TbElement::Windows, 2 * 48}, {TbElement::Clock, 60}};
  TbLayoutParams p;
  p.length = 1000;
  p.windows_fill = false;
  const auto s = tb_layout(items, p);
  EXPECT_DOUBLE_EQ(size_of(s, TbElement::Windows), 96);
}

TEST(TaskbarElements, ZeroSizeElementsTakeNoRoom) {
  std::vector<TbItem> items = {{TbElement::Start, 46}, {TbElement::Pinned, 0}, {TbElement::Tray, 0}, {TbElement::Clock, 60}};
  TbLayoutParams p;
  p.length = 300;
  const auto s = tb_layout(items, p);
  EXPECT_FALSE(has(s, TbElement::Pinned));
  EXPECT_FALSE(has(s, TbElement::Tray));
  EXPECT_DOUBLE_EQ(pos_of(s, TbElement::Clock), 300 - 6 - 60);
}

TEST(TaskbarElements, CenteredStartGroupsTheMainZoneInTheMiddle) {
  std::vector<TbItem> items = {{TbElement::Start, 46}, {TbElement::Pinned, 80}, {TbElement::Windows, 200}, {TbElement::Clock, 60}};
  TbLayoutParams p;
  p.length = 1000;
  p.centered_start = true;
  const auto s = tb_layout(items, p);
  const double total = 46 + 10 + 80 + 10 + 200;
  EXPECT_NEAR(pos_of(s, TbElement::Start), (1000 - total) / 2, 1e-9);
  EXPECT_DOUBLE_EQ(size_of(s, TbElement::Windows), 200);  // only what the buttons want
  EXPECT_DOUBLE_EQ(pos_of(s, TbElement::Clock), 1000 - 6 - 60);
}

TEST(TaskbarElements, CenteredGroupNeverCoversTheStatusZone) {
  std::vector<TbItem> items = {{TbElement::Start, 46}, {TbElement::Windows, 700}, {TbElement::Clock, 200}};
  TbLayoutParams p;
  p.length = 1000;
  p.centered_start = true;
  const auto s = tb_layout(items, p);
  const double end = pos_of(s, TbElement::Windows) + size_of(s, TbElement::Windows);
  EXPECT_LE(end + 10, pos_of(s, TbElement::Clock) + 1e-9);
  EXPECT_GE(pos_of(s, TbElement::Start), 6);
}

TEST(TaskbarElements, HiddenStatusElementsLeaveNoGap) {
  std::vector<TbItem> a = {{TbElement::Start, 46}, {TbElement::Tray, 40}, {TbElement::Clock, 60}};
  std::vector<TbItem> b = {{TbElement::Start, 46}, {TbElement::Clock, 60}};
  TbLayoutParams p;
  p.length = 500;
  EXPECT_DOUBLE_EQ(pos_of(tb_layout(a, p), TbElement::Clock), pos_of(tb_layout(b, p), TbElement::Clock));
}
