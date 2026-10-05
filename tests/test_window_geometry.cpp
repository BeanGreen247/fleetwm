#include <gtest/gtest.h>

#include <vector>

#include "window_geometry.hpp"

using namespace fleetwm::geom;

// ---- resize_edges_at ---------------------------------------------------

TEST(ResizeEdges, InteriorHitsNothing) {
  EXPECT_EQ(resize_edges_at(300, 200, 600, 400), kEdgeNone);
  EXPECT_EQ(resize_edges_at(4, 4, 600, 400) & (kEdgeLeft | kEdgeTop), 0u);  // just inside the 4px band
}

TEST(ResizeEdges, SidesMidway) {
  EXPECT_EQ(resize_edges_at(0, 200, 600, 400), kEdgeLeft);
  EXPECT_EQ(resize_edges_at(599, 200, 600, 400), kEdgeRight);
  EXPECT_EQ(resize_edges_at(300, 0, 600, 400), kEdgeTop);
  EXPECT_EQ(resize_edges_at(300, 399, 600, 400), kEdgeBottom);
}

TEST(ResizeEdges, OutsideRingCountsAsTheNearestSide) {
  EXPECT_EQ(resize_edges_at(-5, 200, 600, 400), kEdgeLeft);
  EXPECT_EQ(resize_edges_at(604, 200, 600, 400), kEdgeRight);
  EXPECT_EQ(resize_edges_at(300, -3, 600, 400), kEdgeTop);
  EXPECT_EQ(resize_edges_at(300, 405, 600, 400), kEdgeBottom);
}

TEST(ResizeEdges, CornersCombineBothAxes) {
  EXPECT_EQ(resize_edges_at(1, 1, 600, 400), kEdgeLeft | kEdgeTop);
  EXPECT_EQ(resize_edges_at(598, 2, 600, 400), kEdgeRight | kEdgeTop);
  EXPECT_EQ(resize_edges_at(1, 398, 600, 400), kEdgeLeft | kEdgeBottom);
  EXPECT_EQ(resize_edges_at(598, 398, 600, 400), kEdgeRight | kEdgeBottom);
}

TEST(ResizeEdges, SideHitNearACornerExtendsIntoIt) {
  // On the left side but within 12px of the top: that is the top-left corner.
  EXPECT_EQ(resize_edges_at(0, 8, 600, 400), kEdgeLeft | kEdgeTop);
  EXPECT_EQ(resize_edges_at(0, 392, 600, 400), kEdgeLeft | kEdgeBottom);
  // On the top side but near the right end.
  EXPECT_EQ(resize_edges_at(595, 0, 600, 400), kEdgeTop | kEdgeRight);
  // 13px along the side is past the corner span.
  EXPECT_EQ(resize_edges_at(0, 13, 600, 400), kEdgeLeft);
}

TEST(ResizeEdges, CustomBandSizes) {
  EXPECT_EQ(resize_edges_at(9, 200, 600, 400, 10, 12), kEdgeLeft);
  EXPECT_EQ(resize_edges_at(9, 200, 600, 400, 4, 12), kEdgeNone);
}

TEST(ResizeEdges, EdgeValuesMatchWlrEdges) {
  // wlr_edges: TOP=1, BOTTOM=2, LEFT=4, RIGHT=8 -- passed straight to wlroots.
  EXPECT_EQ(kEdgeTop, 1u);
  EXPECT_EQ(kEdgeBottom, 2u);
  EXPECT_EQ(kEdgeLeft, 4u);
  EXPECT_EQ(kEdgeRight, 8u);
}

// ---- resized_box -------------------------------------------------------

TEST(ResizedBox, RightAndBottomGrowInPlace) {
  const Box start{100, 50, 400, 300};
  const Box r = resized_box(start, kEdgeRight | kEdgeBottom, 40, 25, 160, 80);
  EXPECT_EQ(r, (Box{100, 50, 440, 325}));
}

TEST(ResizedBox, LeftAndTopKeepTheOppositeEdgeFixed) {
  const Box start{100, 50, 400, 300};
  const Box r = resized_box(start, kEdgeLeft | kEdgeTop, -30, -20, 160, 80);  // drag up-left: grow
  EXPECT_EQ(r, (Box{70, 30, 430, 320}));
  EXPECT_EQ(r.x + r.w, start.x + start.w);
  EXPECT_EQ(r.y + r.h, start.y + start.h);
}

TEST(ResizedBox, ShrinkingLeftEdgeMovesItRight) {
  const Box r = resized_box({100, 50, 400, 300}, kEdgeLeft, 50, 0, 160, 80);
  EXPECT_EQ(r, (Box{150, 50, 350, 300}));
}

TEST(ResizedBox, ClampsToMinimumAndStopsMovingTheOrigin) {
  const Box start{100, 50, 400, 300};
  const Box r = resized_box(start, kEdgeLeft | kEdgeTop, 900, 900, 160, 80);
  EXPECT_EQ(r.w, 160);
  EXPECT_EQ(r.h, 80);
  EXPECT_EQ(r.x, start.x + start.w - 160);  // right edge stays put
  EXPECT_EQ(r.y, start.y + start.h - 80);   // bottom edge stays put
}

TEST(ResizedBox, NoEdgesLeavesBoxAlone) {
  const Box start{10, 20, 300, 200};
  EXPECT_EQ(resized_box(start, kEdgeNone, 99, 99, 160, 80), start);
}

TEST(ResizedBox, SingleAxisIgnoresTheOtherDelta) {
  const Box r = resized_box({0, 0, 400, 300}, kEdgeRight, 25, 999, 160, 80);
  EXPECT_EQ(r, (Box{0, 0, 425, 300}));
}

// ---- cascade_position --------------------------------------------------

TEST(Cascade, FirstWindowIsCenteredNudgedUpLeft) {
  const Box area{0, 40, 1280, 760};
  const Box p = cascade_position(area, 700, 530, 0);
  EXPECT_EQ(p.x, (1280 - 700) / 2 - 112);
  EXPECT_EQ(p.y, 40 + (760 - 530) / 2 - 112);
}

TEST(Cascade, StepsDownRightAndWrapsEveryEight) {
  const Box area{0, 0, 1920, 1080};
  const Box a = cascade_position(area, 600, 400, 0);
  const Box b = cascade_position(area, 600, 400, 1);
  EXPECT_EQ(b.x - a.x, 32);
  EXPECT_EQ(b.y - a.y, 32);
  EXPECT_EQ(cascade_position(area, 600, 400, 8), a);
  EXPECT_EQ(cascade_position(area, 600, 400, 9), b);
}

TEST(Cascade, StaysInsideTheWorkArea) {
  const Box area{0, 40, 800, 600};
  for (int i = 0; i < 16; ++i) {
    const Box p = cascade_position(area, 500, 400, i);
    EXPECT_GE(p.x, area.x);
    EXPECT_GE(p.y, area.y);
    EXPECT_LE(p.x + 500, area.x + area.w);
    EXPECT_LE(p.y + 400, area.y + area.h);
  }
}

TEST(Cascade, WindowLargerThanTheAreaPinsToItsOrigin) {
  const Box area{10, 40, 400, 300};
  const Box p = cascade_position(area, 900, 700, 3);
  EXPECT_EQ(p.x, 10);
  EXPECT_EQ(p.y, 40);
}

TEST(Cascade, NegativeIndexIsTreatedAsFirst) {
  const Box area{0, 0, 1280, 800};
  EXPECT_EQ(cascade_position(area, 600, 400, -1), cascade_position(area, 600, 400, 0));
}

// ---- titlebar layout --------------------------------------------------------

namespace {
TitlebarMetrics metrics() { return TitlebarMetrics{}; }

std::vector<int> ids(const TitlebarLayout& l) {
  std::vector<int> out;
  for (int i = 0; i < l.count; ++i) out.push_back(l.buttons[i].id);
  return out;
}
}  // namespace

TEST(TitlebarLayout, DefaultRightSideOrderHasCloseAtTheEdge) {
  const TitlebarLayout l = layout_titlebar(600, metrics());
  EXPECT_EQ(ids(l), (std::vector<int>{kBtnPin, kBtnMinimize, kBtnMaximize, kBtnClose}));
  EXPECT_DOUBLE_EQ(l.buttons[3].x + l.buttons[3].w, 600.0);  // flush with the right edge
}

TEST(TitlebarLayout, LeftSideOrderHasCloseAtTheEdge) {
  TitlebarMetrics m = metrics();
  m.buttons_right = false;
  const TitlebarLayout l = layout_titlebar(600, m);
  EXPECT_EQ(ids(l), (std::vector<int>{kBtnClose, kBtnMinimize, kBtnMaximize, kBtnPin}));
  EXPECT_DOUBLE_EQ(l.buttons[0].x, 0.0);
}

TEST(TitlebarLayout, ButtonsCanBeHidden) {
  TitlebarMetrics m = metrics();
  m.show_pin = false;
  m.show_minimize = false;
  EXPECT_EQ(ids(layout_titlebar(600, m)), (std::vector<int>{kBtnMaximize, kBtnClose}));
  m.show_maximize = false;
  EXPECT_EQ(ids(layout_titlebar(600, m)), (std::vector<int>{kBtnClose}));  // close is always there
}

TEST(TitlebarLayout, ButtonSizeAndVerticalCentering) {
  TitlebarMetrics m = metrics();
  m.height = 40;
  m.button_w = 50;
  m.button_h = 20;
  const TitlebarLayout l = layout_titlebar(800, m);
  for (int i = 0; i < l.count; ++i) {
    EXPECT_DOUBLE_EQ(l.buttons[i].w, 50.0);
    EXPECT_DOUBLE_EQ(l.buttons[i].h, 20.0);
    EXPECT_DOUBLE_EQ(l.buttons[i].y, 10.0);
  }
  EXPECT_DOUBLE_EQ(l.buttons[0].x, 800.0 - 4 * 50);
}

TEST(TitlebarLayout, ButtonsAreContiguous) {
  const TitlebarLayout l = layout_titlebar(500, metrics());
  for (int i = 1; i < l.count; ++i)
    EXPECT_DOUBLE_EQ(l.buttons[i].x, l.buttons[i - 1].x + l.buttons[i - 1].w);
}

TEST(TitlebarLayout, MetricsAreClamped) {
  TitlebarMetrics m = metrics();
  m.height = 3;
  m.button_w = 1;
  m.button_h = 500;
  const TitlebarLayout l = layout_titlebar(300, m);
  EXPECT_DOUBLE_EQ(l.buttons[0].w, 12.0);
  EXPECT_DOUBLE_EQ(l.buttons[0].h, 16.0);  // clamped to the (raised) height of 16
  EXPECT_DOUBLE_EQ(l.buttons[0].y, 0.0);
}

TEST(TitlebarLayout, TitleSpanLeavesRoomForTheButtons) {
  const TitlebarLayout right = layout_titlebar(600, metrics());
  EXPECT_DOUBLE_EQ(right.title_x0, 8.0);
  EXPECT_DOUBLE_EQ(right.title_x1, 600.0 - 4 * 38 - 8);
  TitlebarMetrics m = metrics();
  m.buttons_right = false;
  const TitlebarLayout left = layout_titlebar(600, m);
  EXPECT_DOUBLE_EQ(left.title_x0, 4 * 38 + 8);
  EXPECT_DOUBLE_EQ(left.title_x1, 592.0);
}

TEST(TitlebarLayout, NarrowWindowNeverProducesAnInvertedSpan) {
  for (bool right : {true, false}) {
    TitlebarMetrics m = metrics();
    m.buttons_right = right;
    for (int w : {1, 40, 100, 160}) {
      const TitlebarLayout l = layout_titlebar(w, m);
      EXPECT_LE(l.title_x0, l.title_x1) << "width " << w << " right " << right;
    }
  }
}

TEST(TitlebarHit, FindsEachButtonByPosition) {
  const TitlebarLayout l = layout_titlebar(600, metrics());
  const double cy = l.buttons[0].y + l.buttons[0].h / 2;
  for (int i = 0; i < l.count; ++i)
    EXPECT_EQ(titlebar_button_at(l, l.buttons[i].x + l.buttons[i].w / 2, cy), l.buttons[i].id);
}

TEST(TitlebarHit, EdgesAreHalfOpen) {
  const TitlebarLayout l = layout_titlebar(600, metrics());
  const ButtonSlot& close = l.buttons[3];
  const double cy = close.y + close.h / 2;
  EXPECT_EQ(titlebar_button_at(l, close.x, cy), kBtnClose);
  EXPECT_EQ(titlebar_button_at(l, close.x + close.w, cy), kBtnNone);  // one px past the right edge
  EXPECT_EQ(titlebar_button_at(l, close.x - 0.1, cy), kBtnMaximize);
}

TEST(TitlebarHit, OutsideTheButtonHeightIsNotAButton) {
  TitlebarMetrics m = metrics();
  m.button_h = 20;  // height 32 -> y in [6, 26)
  const TitlebarLayout l = layout_titlebar(600, m);
  const double x = l.buttons[3].x + 5;
  EXPECT_EQ(titlebar_button_at(l, x, 5.9), kBtnNone);
  EXPECT_EQ(titlebar_button_at(l, x, 6.0), kBtnClose);
  EXPECT_EQ(titlebar_button_at(l, x, 25.9), kBtnClose);
  EXPECT_EQ(titlebar_button_at(l, x, 26.0), kBtnNone);
}

TEST(TitlebarHit, TitleAreaAndOutOfRangeAreNotButtons) {
  const TitlebarLayout l = layout_titlebar(600, metrics());
  EXPECT_EQ(titlebar_button_at(l, 100, 16), kBtnNone);
  EXPECT_EQ(titlebar_button_at(l, -5, 16), kBtnNone);
  EXPECT_EQ(titlebar_button_at(l, 700, 16), kBtnNone);
}

TEST(TitlebarHit, HiddenButtonsAreNotHit) {
  TitlebarMetrics m = metrics();
  m.show_pin = false;
  const TitlebarLayout l = layout_titlebar(600, m);
  // Where the pin used to be is now the title area.
  EXPECT_EQ(titlebar_button_at(l, 600 - 4 * 38 + 5, 16), kBtnNone);
}

TEST(TitleX, LeftAndRightHugTheSpanEdges) {
  const TitlebarLayout l = layout_titlebar(600, metrics());
  EXPECT_DOUBLE_EQ(title_x(l, 100, TitleAlignment::Left, 600), l.title_x0);
  EXPECT_DOUBLE_EQ(title_x(l, 100, TitleAlignment::Right, 600), l.title_x1 - 100);
}

TEST(TitleX, CenterIsCenteredOnTheWholeBarWhenItFits) {
  const TitlebarLayout l = layout_titlebar(600, metrics());
  EXPECT_DOUBLE_EQ(title_x(l, 100, TitleAlignment::Center, 600), 250.0);
}

TEST(TitleX, CenterIsPushedClearOfTheButtons) {
  const TitlebarLayout l = layout_titlebar(400, metrics());  // span is 8 .. 240
  const double x = title_x(l, 200, TitleAlignment::Center, 400);  // ideal 100, but 100+200 > 240
  EXPECT_DOUBLE_EQ(x, 240.0 - 200);
  EXPECT_LE(x + 200, l.title_x1 + 1e-9);
}

TEST(TitleX, TextWiderThanTheSpanStartsAtTheLeftOfTheSpan) {
  const TitlebarLayout l = layout_titlebar(300, metrics());
  for (TitleAlignment a : {TitleAlignment::Left, TitleAlignment::Center, TitleAlignment::Right})
    EXPECT_DOUBLE_EQ(title_x(l, 1000, a, 300), l.title_x0);
}

TEST(TitleX, LeftSideButtonsPushTheTitleRight) {
  TitlebarMetrics m = metrics();
  m.buttons_right = false;
  const TitlebarLayout l = layout_titlebar(600, m);
  EXPECT_GE(title_x(l, 100, TitleAlignment::Center, 600), l.title_x0);
  EXPECT_DOUBLE_EQ(title_x(l, 100, TitleAlignment::Left, 600), 4 * 38 + 8);
}

// ---- taskbar_slots -------------------------------------------------------

TEST(TaskbarSlots, NoWindowsOrNoRoomShowsNothing) {
  EXPECT_EQ(taskbar_slots(500, 0).fit, 0u);
  EXPECT_EQ(taskbar_slots(30, 3).fit, 0u);   // narrower than one minimum button
  EXPECT_EQ(taskbar_slots(0, 3).bw, 0.0);
}

TEST(TaskbarSlots, FewWindowsGetTheMaximumWidth) {
  const TaskbarSlots s = taskbar_slots(1000, 2);
  EXPECT_DOUBLE_EQ(s.bw, 200.0);
  EXPECT_EQ(s.fit, 2u);
}

TEST(TaskbarSlots, ButtonsShrinkToShareTheSpace) {
  const TaskbarSlots s = taskbar_slots(600, 6);
  EXPECT_DOUBLE_EQ(s.bw, 600.0 / 6 - 4);
  EXPECT_EQ(s.fit, 6u);
  EXPECT_LE(s.fit * (s.bw + 4) - 4, 600.0 + 1e-9);
}

TEST(TaskbarSlots, TooManyWindowsStopAtTheMinimumWidth) {
  const TaskbarSlots s = taskbar_slots(300, 40);
  EXPECT_DOUBLE_EQ(s.bw, 44.0);
  EXPECT_EQ(s.fit, static_cast<size_t>((300 + 4) / (44 + 4)));
  EXPECT_LT(s.fit, 40u);
}

TEST(TaskbarSlots, ShownButtonsNeverOverflow) {
  for (size_t n = 1; n <= 30; ++n) {
    for (double avail : {120.0, 333.0, 800.0, 1500.0}) {
      const TaskbarSlots s = taskbar_slots(avail, n);
      if (s.fit == 0) continue;
      EXPECT_LE(s.fit * s.bw + (s.fit - 1) * 4, avail + 1e-9) << "n=" << n << " avail=" << avail;
      EXPECT_LE(s.fit, n);
    }
  }
}
