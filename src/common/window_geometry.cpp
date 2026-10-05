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

TitlebarLayout layout_titlebar(int width, const TitlebarMetrics& in) {
  TitlebarMetrics m = in;
  m.height = std::max(16, m.height);
  m.button_w = std::max(12, m.button_w);
  m.button_h = std::clamp(m.button_h, 8, m.height);

  int ids[4];
  int n = 0;
  if (m.buttons_right) {
    if (m.show_pin) ids[n++] = kBtnPin;
    if (m.show_minimize) ids[n++] = kBtnMinimize;
    if (m.show_maximize) ids[n++] = kBtnMaximize;
    ids[n++] = kBtnClose;
  } else {
    ids[n++] = kBtnClose;
    if (m.show_minimize) ids[n++] = kBtnMinimize;
    if (m.show_maximize) ids[n++] = kBtnMaximize;
    if (m.show_pin) ids[n++] = kBtnPin;
  }

  TitlebarLayout out;
  out.count = n;
  const double cluster = static_cast<double>(n) * m.button_w;
  const double x0 = m.buttons_right ? width - cluster : 0.0;
  const double y = (m.height - m.button_h) / 2.0;
  for (int i = 0; i < n; ++i) {
    out.buttons[i] = {ids[i], x0 + i * m.button_w, y, static_cast<double>(m.button_w),
                      static_cast<double>(m.button_h)};
  }
  constexpr double kMargin = 8;
  if (m.buttons_right) {
    out.title_x0 = kMargin;
    out.title_x1 = std::max(kMargin, width - cluster - kMargin);
  } else {
    out.title_x0 = std::min<double>(width, cluster + kMargin);
    out.title_x1 = std::max<double>(out.title_x0, width - kMargin);
  }
  return out;
}

int titlebar_button_at(const TitlebarLayout& layout, double x, double y) {
  for (int i = 0; i < layout.count; ++i) {
    const ButtonSlot& b = layout.buttons[i];
    if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) return b.id;
  }
  return kBtnNone;
}

double title_x(const TitlebarLayout& layout, double text_w, TitleAlignment align, int width) {
  const double span = layout.title_x1 - layout.title_x0;
  if (text_w >= span) return layout.title_x0;
  switch (align) {
    case TitleAlignment::Left: return layout.title_x0;
    case TitleAlignment::Right: return layout.title_x1 - text_w;
    case TitleAlignment::Center: break;
  }
  const double centered = (width - text_w) / 2.0;
  return std::clamp(centered, layout.title_x0, layout.title_x1 - text_w);
}

TaskbarSlots taskbar_slots(double avail, size_t count, double min_w, double max_w, double gap) {
  if (count == 0 || avail < min_w) return {};
  const double ideal = avail / static_cast<double>(count) - gap;
  TaskbarSlots out;
  out.bw = std::min(max_w, std::max(min_w, ideal));
  out.fit = std::min(count, static_cast<size_t>((avail + gap) / (out.bw + gap)));
  return out;
}

}  // namespace fleetwm::geom
