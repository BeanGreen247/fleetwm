#include "view_metrics.hpp"

#include <algorithm>

namespace fleetwm::fm {

// Lays the groups of an icon or tile view out one under the other, each starting on a new row.
static void grid_groups(ViewMetrics* m, int font_px) {
  if (m->group_starts.empty()) return;
  m->group_head_h = font_px + 18;
  int y = 0;
  const int n = static_cast<int>(m->group_starts.size());
  for (int g = 0; g < n; ++g) {
    const int end = g + 1 < n ? m->group_starts[static_cast<size_t>(g) + 1] : m->count;
    const int items = std::max(0, end - m->group_starts[static_cast<size_t>(g)]);
    m->group_head_y.push_back(y);
    m->group_items_y.push_back(y + m->group_head_h);
    y += m->group_head_h + ((items + m->cols - 1) / m->cols) * m->cell_h;
  }
  m->content_h = y;
  m->rows = y / std::max(1, m->cell_h);
}

bool is_grid_mode(ViewMode mode) {
  return mode == ViewMode::SmallIcons || mode == ViewMode::MediumIcons || mode == ViewMode::LargeIcons || mode == ViewMode::ExtraLargeIcons || mode == ViewMode::Tiles;
}

ViewMetrics compute_metrics(ViewMode mode, int count, int vw, int vh, int icon_px, int row_h, int font_px, int header_h, int details_w, const std::vector<int>* groups) {
  ViewMetrics m;
  if (groups && (mode == ViewMode::Details || is_grid_mode(mode))) m.group_starts = *groups;
  m.mode = mode;
  m.count = count;
  m.view_w = vw;
  m.view_h = vh;
  m.icon = icon_px;
  m.header_h = mode == ViewMode::Details ? header_h : 0;
  const int body_h = std::max(1, vh - m.header_h);
  switch (mode) {
    case ViewMode::Details:
      m.cell_w = std::max(vw, details_w);
      m.cell_h = row_h;
      m.cols = 1;
      m.rows = count + static_cast<int>(m.group_starts.size());
      m.content_w = m.cell_w;
      m.content_h = m.rows * row_h;
      break;
    case ViewMode::List:
      m.cell_w = 230;
      m.cell_h = row_h;
      m.rows = std::max(1, body_h / row_h);
      m.cols = std::max(1, (count + m.rows - 1) / m.rows);
      m.horizontal = true;
      m.content_w = m.cols * m.cell_w;
      m.content_h = m.rows * row_h;
      break;
    case ViewMode::Tiles:
      m.cell_w = 250;
      m.cell_h = std::max(icon_px + 12, 3 * font_px + 10);
      m.cols = std::max(1, vw / m.cell_w);
      m.rows = (count + m.cols - 1) / m.cols;
      m.content_w = vw;
      m.content_h = m.rows * m.cell_h;
      grid_groups(&m, font_px);
      break;
    case ViewMode::Content:
      m.cell_w = vw;
      m.cell_h = std::max(icon_px + 16, 3 * font_px + 14);
      m.cols = 1;
      m.rows = count;
      m.content_w = vw;
      m.content_h = count * m.cell_h;
      break;
    default: {  // icon grids: the label wraps under the icon, two lines
      m.cell_w = std::max(icon_px + 44, 6 * font_px + 14);
      m.cell_h = icon_px + 12 + static_cast<int>(2.6 * font_px);
      m.cols = std::max(1, vw / m.cell_w);
      m.rows = (count + m.cols - 1) / m.cols;
      m.content_w = vw;
      m.content_h = m.rows * m.cell_h;
      grid_groups(&m, font_px);
      break;
    }
  }
  return m;
}

int group_of_item(const ViewMetrics& m, int item) {
  if (m.group_starts.empty() || item < 0) return -1;
  return static_cast<int>(std::upper_bound(m.group_starts.begin(), m.group_starts.end(), item) - m.group_starts.begin()) - 1;
}

int row_of_item(const ViewMetrics& m, int item) {
  if (m.group_starts.empty()) return item;
  const int groups = static_cast<int>(std::upper_bound(m.group_starts.begin(), m.group_starts.end(), item) - m.group_starts.begin());
  return item + groups;
}

void row_info(const ViewMetrics& m, int row, bool* is_header, int* index) {
  *is_header = false;
  if (m.group_starts.empty()) {
    *index = row;
    return;
  }
  // the heading of group g sits on row starts[g] + g; the last group whose heading is at or above `row`
  int lo = 0, hi = static_cast<int>(m.group_starts.size()) - 1, k = -1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    if (m.group_starts[mid] + mid <= row) {
      k = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  if (k >= 0 && m.group_starts[k] + k == row) {
    *is_header = true;
    *index = k;
    return;
  }
  *index = row - (k + 1);
}

ItemRect group_header_rect(const ViewMetrics& m, int g, int sx, int sy) {
  ItemRect r;
  if (m.grid_grouped()) {
    r.w = m.view_w;
    r.h = m.group_head_h;
    r.x = -sx;
    r.y = m.header_h + m.group_head_y[static_cast<size_t>(g)] - sy;
    return r;
  }
  r.w = m.cell_w;
  r.h = m.cell_h;
  r.x = -sx;
  r.y = m.header_h + (m.group_starts[g] + g) * m.cell_h - sy;
  return r;
}

int max_scroll_x(const ViewMetrics& m) { return m.horizontal ? std::max(0, m.content_w - m.view_w) : 0; }
int max_scroll_y(const ViewMetrics& m) { return m.horizontal ? 0 : std::max(0, m.content_h - std::max(1, m.view_h - m.header_h)); }

ItemRect item_rect(const ViewMetrics& m, int i, int sx, int sy) {
  ItemRect r;
  r.w = m.mode == ViewMode::Details || m.mode == ViewMode::Content ? m.cell_w : m.cell_w;
  r.h = m.cell_h;
  if (m.horizontal) {
    const int col = i / m.rows, row = i % m.rows;
    r.x = col * m.cell_w - sx;
    r.y = row * m.cell_h;
  } else if (m.grid_grouped()) {
    const int g = group_of_item(m, i), local = i - m.group_starts[static_cast<size_t>(g)];
    r.x = (local % m.cols) * m.cell_w - sx;
    r.y = m.header_h + m.group_items_y[static_cast<size_t>(g)] + (local / m.cols) * m.cell_h - sy;
  } else {
    const int col = i % m.cols, row = m.group_starts.empty() ? i / m.cols : row_of_item(m, i);
    r.x = col * m.cell_w - sx;
    r.y = m.header_h + row * m.cell_h - sy;
  }
  return r;
}

void visible_range(const ViewMetrics& m, int sx, int sy, int* first, int* last) {
  *first = 0;
  *last = -1;
  if (m.count <= 0) return;
  if (m.horizontal) {
    const int c0 = std::max(0, sx / m.cell_w), c1 = std::min(m.cols - 1, (sx + m.view_w) / m.cell_w);
    *first = c0 * m.rows;
    *last = std::min(m.count - 1, (c1 + 1) * m.rows - 1);
  } else if (m.grid_grouped()) {
    const int top = sy, bottom = sy + std::max(1, m.view_h - m.header_h);
    auto group_at = [&](int y) {
      const int g = static_cast<int>(std::upper_bound(m.group_head_y.begin(), m.group_head_y.end(), y) - m.group_head_y.begin()) - 1;
      return std::clamp(g, 0, static_cast<int>(m.group_starts.size()) - 1);
    };
    const int g0 = group_at(top), g1 = group_at(bottom);
    const size_t a = static_cast<size_t>(g0), b = static_cast<size_t>(g1);
    *first = top <= m.group_items_y[a] ? m.group_starts[a] : m.group_starts[a] + (top - m.group_items_y[a]) / m.cell_h * m.cols;
    const int end1 = g1 + 1 < static_cast<int>(m.group_starts.size()) ? m.group_starts[b + 1] : m.count;
    if (bottom < m.group_items_y[b]) *last = m.group_starts[b] - 1;
    else *last = std::min(end1 - 1, m.group_starts[b] + ((bottom - m.group_items_y[b]) / m.cell_h + 1) * m.cols - 1);
    *last = std::min(*last, m.count - 1);
  } else {
    const int r0 = std::max(0, sy / m.cell_h), r1 = std::min(m.rows - 1, (sy + m.view_h - m.header_h) / m.cell_h);
    if (!m.group_starts.empty()) {
      bool h0, h1;
      int i0, i1;
      row_info(m, r0, &h0, &i0);
      row_info(m, r1, &h1, &i1);
      // a heading row belongs to the group below it: its first item is the next one
      *first = h0 ? m.group_starts[i0] : i0;
      *last = std::min(m.count - 1, h1 ? m.group_starts[i1] - 1 : i1);
      if (*last < *first && m.count > 0) *last = *first;
      return;
    }
    *first = r0 * m.cols;
    *last = std::min(m.count - 1, (r1 + 1) * m.cols - 1);
  }
}

int index_at(const ViewMetrics& m, int x, int y, int sx, int sy) {
  if (x < 0 || y < m.header_h) return -1;
  int idx;
  if (m.horizontal) {
    const int col = (x + sx) / m.cell_w, row = y / m.cell_h;
    if (row >= m.rows) return -1;
    idx = col * m.rows + row;
  } else {
    const int col = x / m.cell_w, row = (y - m.header_h + sy) / m.cell_h;
    if (col >= m.cols) return -1;
    if (m.grid_grouped()) {
      const int yy = y - m.header_h + sy;
      const int g = std::clamp(static_cast<int>(std::upper_bound(m.group_head_y.begin(), m.group_head_y.end(), yy) - m.group_head_y.begin()) - 1, 0, static_cast<int>(m.group_starts.size()) - 1);
      if (yy < m.group_items_y[static_cast<size_t>(g)]) return -1;  // on the heading
      const int end = g + 1 < static_cast<int>(m.group_starts.size()) ? m.group_starts[static_cast<size_t>(g) + 1] : m.count;
      const int i = m.group_starts[static_cast<size_t>(g)] + ((yy - m.group_items_y[static_cast<size_t>(g)]) / m.cell_h) * m.cols + col;
      return i >= 0 && i < end ? i : -1;
    }
    if (!m.group_starts.empty()) {
      bool header;
      int ix;
      row_info(m, row, &header, &ix);
      return header || ix < 0 || ix >= m.count ? -1 : ix;
    }
    idx = row * m.cols + col;
  }
  return idx >= 0 && idx < m.count ? idx : -1;
}

std::vector<int> indices_in_rect(const ViewMetrics& m, int x0, int y0, int x1, int y1, int sx, int sy) {
  std::vector<int> out;
  if (x0 > x1) std::swap(x0, x1);
  if (y0 > y1) std::swap(y0, y1);
  int first, last;
  visible_range(m, sx, sy, &first, &last);
  for (int i = first; i <= last; ++i) {
    const ItemRect r = item_rect(m, i, sx, sy);
    // Icon grids and tiles select by the cell; the wide row views only by the name area at the left.
    const int w = m.mode == ViewMode::Details || m.mode == ViewMode::Content ? std::min(r.w, 260) : r.w;
    if (r.x < x1 && r.x + w > x0 && r.y < y1 && r.y + r.h > y0) out.push_back(i);
  }
  return out;
}

void scroll_to_show(const ViewMetrics& m, int i, int* sx, int* sy) {
  if (i < 0 || i >= m.count) return;
  if (m.horizontal) {
    const int left = (i / m.rows) * m.cell_w;
    if (left < *sx) *sx = left;
    else if (left + m.cell_w > *sx + m.view_w) *sx = left + m.cell_w - m.view_w;
    *sx = std::clamp(*sx, 0, max_scroll_x(m));
  } else {
    const int body = std::max(1, m.view_h - m.header_h);
    int top;
    if (m.grid_grouped()) {
      const int g = group_of_item(m, i);
      top = m.group_items_y[static_cast<size_t>(g)] + ((i - m.group_starts[static_cast<size_t>(g)]) / m.cols) * m.cell_h;
      // the first row of a group shows its heading too
      if (i - m.group_starts[static_cast<size_t>(g)] < m.cols) top = m.group_head_y[static_cast<size_t>(g)];
    } else {
      top = (m.group_starts.empty() ? i / m.cols : row_of_item(m, i)) * m.cell_h;
    }
    const int bottom = top + m.cell_h + (m.grid_grouped() && i - m.group_starts[static_cast<size_t>(group_of_item(m, i))] < m.cols ? m.group_head_h : 0);
    if (top < *sy) *sy = top;
    else if (bottom > *sy + body) *sy = bottom - body;
    *sy = std::clamp(*sy, 0, max_scroll_y(m));
  }
}

int navigate(const ViewMetrics& m, int from, Nav key) {
  if (m.count == 0) return -1;
  if (from < 0) return key == Nav::End || key == Nav::Up || key == Nav::Left ? m.count - 1 : 0;
  const int body = std::max(1, m.view_h - m.header_h);
  const int page_rows = std::max(1, body / m.cell_h);
  if (m.grid_grouped() && (key == Nav::Up || key == Nav::Down || key == Nav::PageUp || key == Nav::PageDown)) {
    int to = from;
    const int steps = key == Nav::PageUp || key == Nav::PageDown ? page_rows : 1;
    for (int s = 0; s < steps; ++s) {
      const int g = group_of_item(m, to), start = m.group_starts[static_cast<size_t>(g)];
      const int end = g + 1 < static_cast<int>(m.group_starts.size()) ? m.group_starts[static_cast<size_t>(g) + 1] : m.count;
      const int col = (to - start) % m.cols;
      if (key == Nav::Up || key == Nav::PageUp) {
        if (to - start >= m.cols) to -= m.cols;
        else if (g > 0) {  // the last row of the group above, same column (or its last cell)
          const int pstart = m.group_starts[static_cast<size_t>(g) - 1], pn = start - pstart;
          to = pstart + std::min(((pn - 1) / m.cols) * m.cols + col, pn - 1);
        }
      } else {
        if (to + m.cols < end) to += m.cols;
        else if (g + 1 < static_cast<int>(m.group_starts.size())) {  // the first row of the group below, same column
          const int nstart = end, nend = g + 2 < static_cast<int>(m.group_starts.size()) ? m.group_starts[static_cast<size_t>(g) + 2] : m.count;
          to = std::min(nstart + col, nend - 1);
        } else if ((end - 1 - start) / m.cols > (to - start) / m.cols) {
          to = end - 1;  // a short last row: Down goes to its last cell
        }
      }
    }
    return std::clamp(to, 0, m.count - 1);
  }
  if (m.grid_grouped() && (key == Nav::Left || key == Nav::Right)) {
    const int g = group_of_item(m, from), start = m.group_starts[static_cast<size_t>(g)];
    const int end = g + 1 < static_cast<int>(m.group_starts.size()) ? m.group_starts[static_cast<size_t>(g) + 1] : m.count;
    const int col = (from - start) % m.cols;
    if (key == Nav::Left) return col == 0 ? from : from - 1;
    return col == m.cols - 1 || from + 1 >= end ? from : from + 1;
  }
  int to = from;
  switch (key) {
    case Nav::Home: to = 0; break;
    case Nav::End: to = m.count - 1; break;
    case Nav::Up: to = m.horizontal ? (from % m.rows == 0 ? from : from - 1) : from - m.cols; break;
    case Nav::Down: to = m.horizontal ? (from % m.rows == m.rows - 1 ? from : from + 1) : from + m.cols; break;
    case Nav::Left: to = m.horizontal ? from - m.rows : (m.cols == 1 ? from : (from % m.cols == 0 ? from : from - 1)); break;
    case Nav::Right: to = m.horizontal ? from + m.rows : (m.cols == 1 ? from : (from % m.cols == m.cols - 1 ? from : from + 1)); break;
    case Nav::PageUp: to = m.horizontal ? from - m.rows * std::max(1, m.view_w / m.cell_w) : from - page_rows * m.cols; break;
    case Nav::PageDown: to = m.horizontal ? from + m.rows * std::max(1, m.view_w / m.cell_w) : from + page_rows * m.cols; break;
  }
  if (key == Nav::Down && to >= m.count && !m.horizontal) to = from / m.cols < (m.count - 1) / m.cols ? m.count - 1 : from;
  if (key == Nav::Right && to >= m.count && m.horizontal) to = m.count - 1 > from ? m.count - 1 : from;
  return std::clamp(to, 0, m.count - 1);
}

}  // namespace fleetwm::fm
