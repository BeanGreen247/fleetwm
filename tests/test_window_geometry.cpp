#include <gtest/gtest.h>

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

// ---- titlebar_button_at ------------------------------------------------

TEST(TitlebarButtons, RightmostIsCloseThenMaximize) {
  constexpr int W = 600, B = 38;
  EXPECT_EQ(titlebar_button_at(W, W - 1, B), 1);
  EXPECT_EQ(titlebar_button_at(W, W - B, B), 1);
  EXPECT_EQ(titlebar_button_at(W, W - B - 1, B), 0);
  EXPECT_EQ(titlebar_button_at(W, W - 2 * B, B), 0);
}

TEST(TitlebarButtons, TitleAreaAndOutOfRangeAreNotButtons) {
  constexpr int W = 600, B = 38;
  EXPECT_EQ(titlebar_button_at(W, W - 2 * B - 1, B), -1);
  EXPECT_EQ(titlebar_button_at(W, 0, B), -1);
  EXPECT_EQ(titlebar_button_at(W, W, B), -1);
  EXPECT_EQ(titlebar_button_at(W, -4, B), -1);
}

TEST(TitlebarButtons, NarrowWindowStillResolves) {
  EXPECT_EQ(titlebar_button_at(70, 69, 38), 1);
  EXPECT_EQ(titlebar_button_at(70, 20, 38), 0);
  EXPECT_EQ(titlebar_button_at(70, 40, 38), 1);
}
