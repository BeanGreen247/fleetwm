#pragma once

#include <cstddef>
#include <cstdint>

// Pure geometry used by the Desktop (floating) window layout: where a pointer
// press on a window counts as a resize handle, how a resize drag changes a
// window's box, where a newly opened window goes, and which titlebar button
// is under the pointer. Kept free of wlroots so it can be unit tested.

namespace fleetwm::geom {

struct Box {
  int x = 0, y = 0, w = 0, h = 0;
  bool operator==(const Box&) const = default;
};

// Same values as wlr_edges (WLR_EDGE_*), so they can be passed straight to and
// from wlroots.
enum Edge : uint32_t {
  kEdgeNone = 0,
  kEdgeTop = 1u << 0,
  kEdgeBottom = 1u << 1,
  kEdgeLeft = 1u << 2,
  kEdgeRight = 1u << 3,
};

// Which resize edges a point (lx, ly), relative to the window's outer top-left
// corner, falls on for a window `outer_w` x `outer_h` px. A point within
// `inner` px of a side (or outside it, in the invisible resize ring) hits that
// side; near a corner (within `corner` px along the neighbouring side) the
// adjacent side is added so corners resize both axes.
uint32_t resize_edges_at(double lx, double ly, int outer_w, int outer_h, int inner = 4,
                         int corner = 12);

// New container position and content size for a resize drag that started with
// `start` (container x, y and content w, h) and has moved by (dx, dy). Dragging
// the left/top edges keeps the opposite edge fixed. Never smaller than
// min_w x min_h.
Box resized_box(const Box& start, uint32_t edges, double dx, double dy, int min_w, int min_h);

// Top-left position for the `index`-th window opened on a work `area`: centered
// (nudged up-left) and stepped down-right 32 px per window, wrapping every 8,
// and clamped so the window stays inside the area when it fits. Only x and y
// of the result are meaningful.
Box cascade_position(const Box& area, int outer_w, int outer_h, int index);

// ---- titlebar -----------------------------------------------------------

// Numeric ids are shared with the compositor's hit testing and renderer.
enum TitleButton : int {
  kBtnNone = -1,
  kBtnMaximize = 0,
  kBtnClose = 1,
  kBtnMinimize = 2,
  kBtnPin = 3,
};

enum class TitleAlignment { Left, Center, Right };

// Everything the titlebar layout depends on (a plain copy of the user's
// [titlebar] settings, so this module stays independent of the config code).
struct TitlebarMetrics {
  int height = 32;        // titlebar height
  int button_w = 38;      // width of one button cell
  int button_h = 24;      // height of a button (centered in the titlebar)
  bool buttons_right = true;
  TitleAlignment align = TitleAlignment::Center;
  bool show_pin = true;
  bool show_minimize = true;
  bool show_maximize = true;
};

struct ButtonSlot {
  int id = kBtnNone;
  double x = 0, y = 0, w = 0, h = 0;  // titlebar-local px
};

struct TitlebarLayout {
  ButtonSlot buttons[4];
  int count = 0;
  // The part of the bar the title may use (between the buttons and the far
  // margin), in titlebar-local px.
  double title_x0 = 0, title_x1 = 0;
};

// Lays out the buttons for a titlebar `width` px wide. On the right side the
// order is pin, minimize, maximize, close (close at the screen edge); on the
// left it is close, minimize, maximize, pin (close at the edge again).
// Metrics are clamped to sane ranges (height >= 16, button_w >= 12,
// 8 <= button_h <= height).
TitlebarLayout layout_titlebar(int width, const TitlebarMetrics& m);

// The button under (x, y) (titlebar-local), or kBtnNone.
int titlebar_button_at(const TitlebarLayout& layout, double x, double y);

// Left edge of a title `text_w` px wide, honouring the alignment and never
// overlapping the buttons: centered text is centered on the whole bar when it
// fits, otherwise pushed inside the free span; text wider than the span
// starts at its left edge.
double title_x(const TitlebarLayout& layout, double text_w, TitleAlignment align, int width);

// ---- snapping -------------------------------------------------------------

// Where a dragged window lands when released near a screen edge: halves on
// the left/right edges, quarters in the corners, maximized on the top edge.
enum class SnapZone {
  None,
  Maximize,
  Left,
  Right,
  TopLeft,
  TopRight,
  BottomLeft,
  BottomRight,
};

// The zone a pointer at (px, py) is in, for an output whose full box is
// `screen`. A zone triggers within `edge` px of a screen edge; within `corner`
// px of a corner it becomes the quarter instead of the half. There is no zone
// on the bottom edge's middle (that would only ever be a mistake).
SnapZone snap_zone_at(double px, double py, const Box& screen, int edge = 10, int corner = 64);

// The window's outer box (including titlebar and border) for `zone` inside the
// work area `area`. Halves split the width, quarters split both; odd sizes give
// the extra pixel to the right/bottom piece so the pieces tile exactly.
// SnapZone::None yields an empty box.
Box snap_box(SnapZone zone, const Box& area);

// How window buttons share `avail` px of a horizontal taskbar: each button is
// `bw` wide (between min_w and max_w, shrinking as windows are added) with `gap`
// px between buttons, and only `fit` of the `count` windows are shown.
struct TaskbarSlots {
  double bw = 0;
  size_t fit = 0;
};
TaskbarSlots taskbar_slots(double avail, size_t count, double min_w = 44, double max_w = 200,
                           double gap = 4);

}  // namespace fleetwm::geom
