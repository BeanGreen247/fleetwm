#include "window_geometry.hpp"

#include <algorithm>

namespace fleetwm::geom {

uint32_t resize_edges_at(double lx, double ly, int outer_w, int outer_h, int inner, int corner) {
  uint32_t edges = kEdgeNone;
  if (lx < inner) edges |= kEdgeLeft;
  if (lx >= outer_w - inner) edges |= kEdgeRight;
  if (ly < inner) edges |= kEdgeTop;
  if (ly >= outer_h - inner) edges |= kEdgeBottom;

  const bool horizontal = edges & (kEdgeLeft | kEdgeRight);
  const bool vertical = edges & (kEdgeTop | kEdgeBottom);
  if (horizontal && !vertical) {
    if (ly < corner) edges |= kEdgeTop;
    else if (ly >= outer_h - corner) edges |= kEdgeBottom;
  } else if (vertical && !horizontal) {
    if (lx < corner) edges |= kEdgeLeft;
    else if (lx >= outer_w - corner) edges |= kEdgeRight;
  }
  return edges;
}

Box resized_box(const Box& start, uint32_t edges, double dx, double dy, int min_w, int min_h) {
  int w = start.w, h = start.h;
  if (edges & kEdgeRight) w = start.w + static_cast<int>(dx);
  if (edges & kEdgeLeft) w = start.w - static_cast<int>(dx);
  if (edges & kEdgeBottom) h = start.h + static_cast<int>(dy);
  if (edges & kEdgeTop) h = start.h - static_cast<int>(dy);
  w = std::max(w, min_w);
  h = std::max(h, min_h);

  Box out{start.x, start.y, w, h};
  if (edges & kEdgeLeft) out.x = start.x + (start.w - w);
  if (edges & kEdgeTop) out.y = start.y + (start.h - h);
  return out;
}

Box cascade_position(const Box& area, int outer_w, int outer_h, int index) {
  const int step = (std::max(0, index) % 8) * 32;
  Box out;
  out.x = std::clamp(area.x + (area.w - outer_w) / 2 + step - 112, area.x,
                     area.x + std::max(0, area.w - outer_w));
  out.y = std::clamp(area.y + (area.h - outer_h) / 2 + step - 112, area.y,
                     area.y + std::max(0, area.h - outer_h));
  out.w = outer_w;
  out.h = outer_h;
  return out;
}

int titlebar_button_at(int width, double x, int button_w) {
  if (x >= width - button_w && x < width) return 1;
  if (x >= width - 2 * button_w && x < width - button_w) return 0;
  if (x >= width - 3 * button_w && x < width - 2 * button_w) return 2;
  return -1;
}

}  // namespace fleetwm::geom
