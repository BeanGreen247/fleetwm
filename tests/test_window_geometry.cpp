#include <gtest/gtest.h>

#include <cmath>
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

// ---- snapping --------------------------------------------------------------

namespace {
const Box kScreen{0, 0, 1920, 1080};
}

TEST(SnapZoneAt, InteriorIsNotAZone) {
  EXPECT_EQ(snap_zone_at(960, 540, kScreen), SnapZone::None);
  EXPECT_EQ(snap_zone_at(11, 540, kScreen), SnapZone::None);    // just outside the 10px band
  EXPECT_EQ(snap_zone_at(960, 11, kScreen), SnapZone::None);
}

TEST(SnapZoneAt, SideEdgesGiveHalves) {
  EXPECT_EQ(snap_zone_at(0, 540, kScreen), SnapZone::Left);
  EXPECT_EQ(snap_zone_at(9, 540, kScreen), SnapZone::Left);
  EXPECT_EQ(snap_zone_at(1919, 540, kScreen), SnapZone::Right);
  EXPECT_EQ(snap_zone_at(1910, 540, kScreen), SnapZone::Right);
}

TEST(SnapZoneAt, TopEdgeMaximizes) {
  EXPECT_EQ(snap_zone_at(960, 0, kScreen), SnapZone::Maximize);
  EXPECT_EQ(snap_zone_at(960, 9, kScreen), SnapZone::Maximize);
}

TEST(SnapZoneAt, BottomEdgeMiddleIsNothing) {
  EXPECT_EQ(snap_zone_at(960, 1079, kScreen), SnapZone::None);
}

TEST(SnapZoneAt, CornersGiveQuarters) {
  EXPECT_EQ(snap_zone_at(0, 0, kScreen), SnapZone::TopLeft);
  EXPECT_EQ(snap_zone_at(1919, 0, kScreen), SnapZone::TopRight);
  EXPECT_EQ(snap_zone_at(0, 1079, kScreen), SnapZone::BottomLeft);
  EXPECT_EQ(snap_zone_at(1919, 1079, kScreen), SnapZone::BottomRight);
}

TEST(SnapZoneAt, CornerReachesAlongBothEdges) {
  EXPECT_EQ(snap_zone_at(0, 63, kScreen), SnapZone::TopLeft);       // left edge, within 64px of the top
  EXPECT_EQ(snap_zone_at(0, 64, kScreen), SnapZone::Left);          // just past it
  EXPECT_EQ(snap_zone_at(63, 0, kScreen), SnapZone::TopLeft);       // top edge, within 64px of the left
  EXPECT_EQ(snap_zone_at(64, 0, kScreen), SnapZone::Maximize);
  EXPECT_EQ(snap_zone_at(1919, 1079 - 63, kScreen), SnapZone::BottomRight);
  EXPECT_EQ(snap_zone_at(1919, 1079 - 64, kScreen), SnapZone::Right);
  EXPECT_EQ(snap_zone_at(63, 1079, kScreen), SnapZone::BottomLeft);
  EXPECT_EQ(snap_zone_at(1919 - 63, 1079, kScreen), SnapZone::BottomRight);
}

TEST(SnapZoneAt, OutsideTheScreenIsNothing) {
  EXPECT_EQ(snap_zone_at(-1, 540, kScreen), SnapZone::None);
  EXPECT_EQ(snap_zone_at(1920, 540, kScreen), SnapZone::None);
  EXPECT_EQ(snap_zone_at(960, -1, kScreen), SnapZone::None);
  EXPECT_EQ(snap_zone_at(960, 1080, kScreen), SnapZone::None);
}

TEST(SnapZoneAt, WorksOnAnOutputNotAtTheOrigin) {
  const Box second{1920, 100, 1280, 720};
  EXPECT_EQ(snap_zone_at(1920, 400, second), SnapZone::Left);
  EXPECT_EQ(snap_zone_at(3199, 400, second), SnapZone::Right);
  EXPECT_EQ(snap_zone_at(2500, 100, second), SnapZone::Maximize);
  EXPECT_EQ(snap_zone_at(1919, 400, second), SnapZone::None);  // on the neighbouring output
}

TEST(SnapZoneAt, EdgeAndCornerSizesAreAdjustable) {
  EXPECT_EQ(snap_zone_at(30, 540, kScreen, 40, 64), SnapZone::Left);
  EXPECT_EQ(snap_zone_at(30, 540, kScreen, 10, 64), SnapZone::None);
  EXPECT_EQ(snap_zone_at(0, 100, kScreen, 10, 120), SnapZone::TopLeft);
}

TEST(SnapBox, NoneIsEmptyAndMaximizeIsTheWholeArea) {
  const Box area{0, 40, 1280, 720};
  EXPECT_EQ(snap_box(SnapZone::None, area), (Box{}));
  EXPECT_EQ(snap_box(SnapZone::Maximize, area), area);
}

TEST(SnapBox, HalvesTileTheArea) {
  const Box area{0, 40, 1280, 720};
  EXPECT_EQ(snap_box(SnapZone::Left, area), (Box{0, 40, 640, 720}));
  EXPECT_EQ(snap_box(SnapZone::Right, area), (Box{640, 40, 640, 720}));
}

TEST(SnapBox, QuartersTileTheArea) {
  const Box area{10, 20, 1000, 600};
  EXPECT_EQ(snap_box(SnapZone::TopLeft, area), (Box{10, 20, 500, 300}));
  EXPECT_EQ(snap_box(SnapZone::TopRight, area), (Box{510, 20, 500, 300}));
  EXPECT_EQ(snap_box(SnapZone::BottomLeft, area), (Box{10, 320, 500, 300}));
  EXPECT_EQ(snap_box(SnapZone::BottomRight, area), (Box{510, 320, 500, 300}));
}

TEST(SnapBox, OddSizesStillTileWithoutGapsOrOverlap) {
  const Box area{3, 7, 1001, 603};
  const Box l = snap_box(SnapZone::Left, area), r = snap_box(SnapZone::Right, area);
  EXPECT_EQ(l.x + l.w, r.x);
  EXPECT_EQ(r.x + r.w, area.x + area.w);
  const Box tl = snap_box(SnapZone::TopLeft, area), bl = snap_box(SnapZone::BottomLeft, area);
  EXPECT_EQ(tl.y + tl.h, bl.y);
  EXPECT_EQ(bl.y + bl.h, area.y + area.h);
  const Box br = snap_box(SnapZone::BottomRight, area);
  EXPECT_EQ(br.x + br.w, area.x + area.w);
  EXPECT_EQ(br.y + br.h, area.y + area.h);
}

TEST(SnapBox, EveryZoneStaysInsideTheArea) {
  const Box area{0, 40, 1366, 728};
  for (SnapZone z : {SnapZone::Maximize, SnapZone::Left, SnapZone::Right, SnapZone::TopLeft,
                     SnapZone::TopRight, SnapZone::BottomLeft, SnapZone::BottomRight}) {
    const Box b = snap_box(z, area);
    EXPECT_GE(b.x, area.x);
    EXPECT_GE(b.y, area.y);
    EXPECT_LE(b.x + b.w, area.x + area.w);
    EXPECT_LE(b.y + b.h, area.y + area.h);
    EXPECT_GT(b.w, 0);
    EXPECT_GT(b.h, 0);
  }
}

// ---- exclusive_edge ----------------------------------------------------------

TEST(ExclusiveEdge, FullWidthBarsReserveTheirEdge) {
  EXPECT_EQ(exclusive_edge(true, true, true, false), BarEdge::Top);       // capsule / strip / top taskbar
  EXPECT_EQ(exclusive_edge(true, true, false, true), BarEdge::Bottom);    // bottom taskbar
  EXPECT_EQ(exclusive_edge(true, false, true, true), BarEdge::Left);      // left taskbar
  EXPECT_EQ(exclusive_edge(false, true, true, true), BarEdge::Right);     // right taskbar
}

TEST(ExclusiveEdge, CenteredIslandBarAnchoredToOneEdgeReservesIt) {
  EXPECT_EQ(exclusive_edge(false, false, true, false), BarEdge::Top);     // the Island bar
  EXPECT_EQ(exclusive_edge(false, false, false, true), BarEdge::Bottom);
  EXPECT_EQ(exclusive_edge(true, false, false, false), BarEdge::Left);
  EXPECT_EQ(exclusive_edge(false, true, false, false), BarEdge::Right);
}

TEST(ExclusiveEdge, AmbiguousAnchorsReserveNothing) {
  EXPECT_EQ(exclusive_edge(false, false, false, false), BarEdge::None);   // launcher, tooltips
  EXPECT_EQ(exclusive_edge(true, true, true, true), BarEdge::None);       // full-screen overlay
  EXPECT_EQ(exclusive_edge(true, false, true, false), BarEdge::None);     // a corner
  EXPECT_EQ(exclusive_edge(false, true, false, true), BarEdge::None);
  EXPECT_EQ(exclusive_edge(true, true, false, false), BarEdge::None);     // spans x but no vertical edge
  EXPECT_EQ(exclusive_edge(false, false, true, true), BarEdge::None);
}

// ---- tile_boxes / tile_zones ----------------------------------------------------

namespace {
const Box kArea{0, 40, 1280, 721};  // odd height on purpose
}

TEST(TileBoxes, NoWindowsNoBoxes) {
  EXPECT_TRUE(tile_boxes(kArea, 0).empty());
  EXPECT_TRUE(tile_zones(0).empty());
}

TEST(TileBoxes, OneWindowFillsTheArea) {
  EXPECT_EQ(tile_boxes(kArea, 1), (std::vector<Box>{kArea}));
}

TEST(TileBoxes, MasterTakesTheLeftHalf) {
  const auto b = tile_boxes(kArea, 4);
  EXPECT_EQ(b[0], (Box{0, 40, 640, 721}));
}

TEST(TileBoxes, StackSharesTheRightHalfAndTilesExactly) {
  for (size_t n = 2; n <= 9; ++n) {
    const auto b = tile_boxes(kArea, n);
    ASSERT_EQ(b.size(), n);
    int y = kArea.y;
    for (size_t i = 1; i < n; ++i) {
      EXPECT_EQ(b[i].x, 640) << n;
      EXPECT_EQ(b[i].w, 640) << n;
      EXPECT_EQ(b[i].y, y) << n;
      y += b[i].h;
    }
    EXPECT_EQ(y, kArea.y + kArea.h) << "stack must end at the bottom for n=" << n;
  }
}

TEST(TileBoxes, StackWindowsAreNearlyEqualHeight) {
  const auto b = tile_boxes(kArea, 4);  // 3 stacked in 721px
  EXPECT_EQ(b[1].h, 240);
  EXPECT_EQ(b[2].h, 240);
  EXPECT_EQ(b[3].h, 241);  // the last one takes the remainder
}

TEST(TileBoxes, OddWidthStillCoversTheArea) {
  const Box a{5, 5, 1001, 500};
  const auto b = tile_boxes(a, 2);
  EXPECT_EQ(b[0].x + b[0].w, b[1].x);
  EXPECT_EQ(b[1].x + b[1].w, a.x + a.w);
}

TEST(TileZones, MatchTheSnapBoxesForUpToThreeWindows) {
  for (size_t n = 1; n <= 3; ++n) {
    const auto zones = tile_zones(n);
    const auto boxes = tile_boxes(kArea, n);
    ASSERT_EQ(zones.size(), n);
    for (size_t i = 0; i < n; ++i)
      EXPECT_EQ(snap_box(zones[i], kArea), boxes[i]) << "n=" << n << " i=" << i;
  }
}

TEST(TileZones, MoreThanThreeWindowsHaveNoZones) {
  for (size_t n : {4u, 5u, 12u}) {
    const auto zones = tile_zones(n);
    ASSERT_EQ(zones.size(), n);
    for (SnapZone z : zones) EXPECT_EQ(z, SnapZone::None);
  }
}

// ---- snap_step (Super+arrows) -------------------------------------------------

namespace {
using K = SnapStep::Kind;
SnapStep to(SnapZone z) { return {K::Zone, z}; }
}  // namespace

TEST(SnapStep, LeftSnapsNormalAndMaximizedWindowsToTheLeftHalf) {
  EXPECT_EQ(snap_step(SnapZone::None, Direction::Left), to(SnapZone::Left));
  EXPECT_EQ(snap_step(SnapZone::Maximize, Direction::Left), to(SnapZone::Left));
}

TEST(SnapStep, RightSnapsNormalAndMaximizedWindowsToTheRightHalf) {
  EXPECT_EQ(snap_step(SnapZone::None, Direction::Right), to(SnapZone::Right));
  EXPECT_EQ(snap_step(SnapZone::Maximize, Direction::Right), to(SnapZone::Right));
}

TEST(SnapStep, OppositeKeyUndoesAHalfSnap) {
  EXPECT_EQ(snap_step(SnapZone::Right, Direction::Left).kind, K::Restore);
  EXPECT_EQ(snap_step(SnapZone::Left, Direction::Right).kind, K::Restore);
}

TEST(SnapStep, SameKeyAgainMovesToTheNeighbouringScreen) {
  EXPECT_EQ(snap_step(SnapZone::Left, Direction::Left).kind, K::MovePrevScreen);
  EXPECT_EQ(snap_step(SnapZone::Right, Direction::Right).kind, K::MoveNextScreen);
  // ...and, like Windows, lands on the half next to the screen it came from
  EXPECT_EQ(snap_step(SnapZone::Left, Direction::Left).zone, SnapZone::Right);
  EXPECT_EQ(snap_step(SnapZone::Right, Direction::Right).zone, SnapZone::Left);
}

TEST(SnapStep, UpMaximizesOrMakesATopQuarter) {
  EXPECT_EQ(snap_step(SnapZone::None, Direction::Up), to(SnapZone::Top));
  EXPECT_EQ(snap_step(SnapZone::Top, Direction::Up), to(SnapZone::Maximize));
  EXPECT_EQ(snap_step(SnapZone::Left, Direction::Up), to(SnapZone::TopLeft));
  EXPECT_EQ(snap_step(SnapZone::Right, Direction::Up), to(SnapZone::TopRight));
  EXPECT_EQ(snap_step(SnapZone::TopLeft, Direction::Up), to(SnapZone::Maximize));
  EXPECT_EQ(snap_step(SnapZone::TopRight, Direction::Up), to(SnapZone::Maximize));
  EXPECT_EQ(snap_step(SnapZone::Maximize, Direction::Up).kind, K::Nothing);
}

TEST(SnapStep, UpFromABottomQuarterGoesBackToTheHalf) {
  EXPECT_EQ(snap_step(SnapZone::BottomLeft, Direction::Up), to(SnapZone::Left));
  EXPECT_EQ(snap_step(SnapZone::BottomRight, Direction::Up), to(SnapZone::Right));
}

TEST(SnapStep, DownRestoresMinimizesOrMakesABottomQuarter) {
  EXPECT_EQ(snap_step(SnapZone::Maximize, Direction::Down).kind, K::Restore);
  EXPECT_EQ(snap_step(SnapZone::None, Direction::Down), to(SnapZone::Bottom));
  EXPECT_EQ(snap_step(SnapZone::Left, Direction::Down), to(SnapZone::BottomLeft));
  EXPECT_EQ(snap_step(SnapZone::Right, Direction::Down), to(SnapZone::BottomRight));
  EXPECT_EQ(snap_step(SnapZone::TopLeft, Direction::Down), to(SnapZone::Left));
  EXPECT_EQ(snap_step(SnapZone::TopRight, Direction::Down), to(SnapZone::Right));
  EXPECT_EQ(snap_step(SnapZone::BottomLeft, Direction::Down).kind, K::Restore);
  EXPECT_EQ(snap_step(SnapZone::BottomRight, Direction::Down).kind, K::Restore);
}

TEST(SnapStep, DownNeverMinimizes) {
  for (SnapZone z : {SnapZone::None, SnapZone::Maximize, SnapZone::Left, SnapZone::Right, SnapZone::TopLeft,
                     SnapZone::TopRight, SnapZone::BottomLeft, SnapZone::BottomRight, SnapZone::Top, SnapZone::Bottom})
    EXPECT_NE(snap_step(z, Direction::Down).kind, K::Minimize);
}

TEST(SnapStep, DownFromMaximizedGoesFloatingThenBottomHalfAndStops) {
  EXPECT_EQ(snap_step(SnapZone::Maximize, Direction::Down).kind, K::Restore);
  EXPECT_EQ(snap_step(SnapZone::None, Direction::Down), to(SnapZone::Bottom));
  EXPECT_EQ(snap_step(SnapZone::Bottom, Direction::Down).kind, K::Nothing);
}

TEST(SnapStep, UpGoesFloatingToTopHalfToMaximizedAndBackFromTheBottom) {
  EXPECT_EQ(snap_step(SnapZone::None, Direction::Up), to(SnapZone::Top));
  EXPECT_EQ(snap_step(SnapZone::Top, Direction::Up), to(SnapZone::Maximize));
  EXPECT_EQ(snap_step(SnapZone::Bottom, Direction::Up).kind, K::Restore);
  EXPECT_EQ(snap_step(SnapZone::Top, Direction::Down).kind, K::Restore);
}

TEST(SnapBox, TopAndBottomAreFullWidthHalves) {
  const Box a{0, 0, 1000, 800};
  EXPECT_EQ(snap_box(SnapZone::Top, a), (Box{0, 0, 1000, 400}));
  EXPECT_EQ(snap_box(SnapZone::Bottom, a), (Box{0, 400, 1000, 400}));
}

TEST(SnapStep, SideKeysMoveBetweenQuarters) {
  EXPECT_EQ(snap_step(SnapZone::TopRight, Direction::Left), to(SnapZone::TopLeft));
  EXPECT_EQ(snap_step(SnapZone::BottomRight, Direction::Left), to(SnapZone::BottomLeft));
  EXPECT_EQ(snap_step(SnapZone::TopLeft, Direction::Right), to(SnapZone::TopRight));
  EXPECT_EQ(snap_step(SnapZone::BottomLeft, Direction::Right), to(SnapZone::BottomRight));
  EXPECT_EQ(snap_step(SnapZone::TopLeft, Direction::Left), to(SnapZone::Left));
  EXPECT_EQ(snap_step(SnapZone::BottomRight, Direction::Right), to(SnapZone::Right));
}

TEST(SnapStep, EveryStateAndKeyGivesADefinedAnswer) {
  for (SnapZone z : {SnapZone::None, SnapZone::Maximize, SnapZone::Left, SnapZone::Right, SnapZone::TopLeft,
                     SnapZone::TopRight, SnapZone::BottomLeft, SnapZone::BottomRight, SnapZone::Top, SnapZone::Bottom})
    for (Direction d : {Direction::Left, Direction::Right, Direction::Up, Direction::Down}) {
      const SnapStep s = snap_step(z, d);
      if (s.kind == K::Zone) EXPECT_NE(s.zone, SnapZone::None);  // a snap always names a target
    }
}

TEST(SnapStep, FollowingTheKeysNeverGetsStuck) {
  // From a normal window, Right then Left returns to the starting state.
  EXPECT_EQ(snap_step(snap_step(SnapZone::None, Direction::Right).zone, Direction::Left).kind, K::Restore);
  // A maximized window can always be brought back with Down.
  EXPECT_EQ(snap_step(SnapZone::Maximize, Direction::Down).kind, K::Restore);
}

// ---- the joined caption strip (Windows 7 look) -----------------------------------------------------------

namespace {
TitlebarMetrics strip_metrics() {
  TitlebarMetrics m = metrics();
  m.strip = true;
  return m;
}
}  // namespace

TEST(TitlebarStrip, HangsFromTheTopRightCornerWithNoGaps) {
  const TitlebarMetrics m = strip_metrics();
  const TitlebarLayout l = layout_titlebar(600, m);
  ASSERT_EQ(l.count, 4);
  EXPECT_EQ(ids(l), (std::vector<int>{kBtnPin, kBtnMinimize, kBtnMaximize, kBtnClose}));
  for (int i = 0; i < l.count; ++i) {
    EXPECT_DOUBLE_EQ(l.buttons[i].y, 0.0) << "the strip lies on the top edge of the bar";
    EXPECT_DOUBLE_EQ(l.buttons[i].h, m.button_h);
    if (i > 0) EXPECT_DOUBLE_EQ(l.buttons[i].x, l.buttons[i - 1].x + l.buttons[i - 1].w) << "buttons touch";
  }
  EXPECT_DOUBLE_EQ(l.buttons[3].x + l.buttons[3].w, 600.0) << "flush with the window's right edge";
}

TEST(TitlebarStrip, CloseIsWiderThanTheOthers) {
  const TitlebarMetrics m = strip_metrics();
  const TitlebarLayout l = layout_titlebar(600, m);
  for (int i = 0; i < 3; ++i) EXPECT_DOUBLE_EQ(l.buttons[i].w, m.button_w);
  EXPECT_DOUBLE_EQ(l.buttons[3].w, std::round(m.button_w * kStripCloseScale));
  EXPECT_GT(l.buttons[3].w, l.buttons[2].w * 1.4);
  EXPECT_LT(l.buttons[3].w, l.buttons[2].w * 1.8);
}

TEST(TitlebarStrip, LeftSideStripIsFlushWithTheLeftEdgeAndCloseStillAtTheEdge) {
  TitlebarMetrics m = strip_metrics();
  m.buttons_right = false;
  const TitlebarLayout l = layout_titlebar(600, m);
  EXPECT_EQ(ids(l), (std::vector<int>{kBtnClose, kBtnMinimize, kBtnMaximize, kBtnPin}));
  EXPECT_DOUBLE_EQ(l.buttons[0].x, 0.0);
  EXPECT_DOUBLE_EQ(l.buttons[0].w, std::round(m.button_w * kStripCloseScale));
  EXPECT_DOUBLE_EQ(l.buttons[0].y, 0.0);
}

TEST(TitlebarStrip, HitTestingFollowsTheWiderCloseButton) {
  const TitlebarMetrics m = strip_metrics();
  const TitlebarLayout l = layout_titlebar(600, m);
  const double close_x = l.buttons[3].x;
  EXPECT_EQ(titlebar_button_at(l, 599, 0), kBtnClose) << "the very corner";
  EXPECT_EQ(titlebar_button_at(l, close_x + 1, 1), kBtnClose);
  EXPECT_EQ(titlebar_button_at(l, close_x - 1, 1), kBtnMaximize);
  EXPECT_EQ(titlebar_button_at(l, l.buttons[0].x + 1, 1), kBtnPin);
  EXPECT_EQ(titlebar_button_at(l, 599, m.button_h + 1), kBtnNone) << "below the strip is the bar again";
  EXPECT_EQ(titlebar_button_at(l, l.buttons[0].x - 1, 1), kBtnNone);
}

TEST(TitlebarStrip, TitleSpanStopsBeforeTheWholeStrip) {
  const TitlebarMetrics m = strip_metrics();
  const TitlebarLayout l = layout_titlebar(600, m);
  EXPECT_LE(l.title_x1, l.buttons[0].x);
  EXPECT_GE(l.title_x1, l.title_x0);
  TitlebarLayout narrow = layout_titlebar(60, m);
  EXPECT_GE(narrow.title_x1, narrow.title_x0) << "a narrow window never gets an inverted span";
}

TEST(TitlebarStrip, HiddenButtonsLeaveTheRestJoined) {
  TitlebarMetrics m = strip_metrics();
  m.show_pin = false;
  m.show_minimize = false;
  const TitlebarLayout l = layout_titlebar(600, m);
  ASSERT_EQ(l.count, 2);
  EXPECT_EQ(ids(l), (std::vector<int>{kBtnMaximize, kBtnClose}));
  EXPECT_DOUBLE_EQ(l.buttons[0].x + l.buttons[0].w, l.buttons[1].x);
  EXPECT_DOUBLE_EQ(l.buttons[1].x + l.buttons[1].w, 600.0);
}

TEST(TitlebarStrip, OffKeepsTheSeparateCentredButtons) {
  const TitlebarMetrics m = metrics();
  ASSERT_FALSE(m.strip) << "the strip is opt-in in the geometry module; the compositor turns it on";
  const TitlebarLayout l = layout_titlebar(600, m);
  EXPECT_DOUBLE_EQ(l.buttons[3].w, m.button_w);
  EXPECT_DOUBLE_EQ(l.buttons[0].y, (m.height - m.button_h) / 2.0);
}
