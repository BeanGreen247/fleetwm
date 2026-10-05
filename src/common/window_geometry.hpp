#pragma once

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

// Titlebar buttons, right-aligned, each `button_w` wide: 0 = maximize
// (second from the right), 1 = close (rightmost), -1 = none.
int titlebar_button_at(int width, double x, int button_w);

}  // namespace fleetwm::geom
