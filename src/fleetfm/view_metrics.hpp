#pragma once

#include <vector>

#include "view_style.hpp"

// Where each item of a folder sits in every view mode, as pure arithmetic: which items are on screen, which one is under the
// pointer, which ones a rubber band touches, where the arrow keys go. Drawing and input both ask this, so they cannot
// disagree, and only the visible items are ever drawn however many the folder holds.

namespace fleetwm::fm {

struct ItemRect {
  int x = 0, y = 0, w = 0, h = 0;  // relative to the content area, scroll already applied
};

struct ViewMetrics {
  // Details view with "Group by": the item index where each group starts. A group has a heading row above its first item, so row = item +
  // (groups started at or before it). Empty = no grouping. Other views list items without headings.
  std::vector<int> group_starts;
  // The icon and tile views group too: each group is a full-width heading with its own grid of cells under it, every group starting on a new
  // row. group_head_y / group_items_y are where each heading and each group's first row of cells sit (content coordinates, below the column
  // heading); group_head_h is the heading's height. Empty for the other views.
  std::vector<int> group_head_y, group_items_y;
  int group_head_h = 0;
  bool grid_grouped() const { return !group_head_y.empty(); }
  ViewMode mode = ViewMode::Details;
  int count = 0;
  int view_w = 0, view_h = 0;      // the content area
  int cell_w = 0, cell_h = 0;
  int cols = 1, rows = 0;          // grid size (rows is how many fit down a column in List mode)
  bool horizontal = false;         // scrolls sideways (List)
  int icon = 16;
  int header_h = 0;                // details column heading, drawn above the rows and not scrolled
  int content_w = 0, content_h = 0;  // the size of everything, for the scroll bars
};

// `row_h` is the details/list row height; `icon_px` the icon edge of icon modes; `font_px` sizes the labels.
ViewMetrics compute_metrics(ViewMode mode, int count, int view_w, int view_h, int icon_px, int row_h, int font_px, int header_h = 0, int details_width = 0,
                            const std::vector<int>* group_starts = nullptr);
// True for the views that lay items out as a grid of cells (icons and tiles).
bool is_grid_mode(ViewMode mode);
// The group an item is in (grouped views), or -1.
int group_of_item(const ViewMetrics& m, int item);
// Which row a grouped Details item is on, and the other way: is_header / group index when the row is a heading, else the item index.
int row_of_item(const ViewMetrics& m, int item);
void row_info(const ViewMetrics& m, int row, bool* is_header, int* index);
// Where the heading of group g is (Details: one row; the icon views: a full-width strip of group_head_h).
ItemRect group_header_rect(const ViewMetrics& m, int g, int scroll_x, int scroll_y);

ItemRect item_rect(const ViewMetrics& m, int index, int scroll_x, int scroll_y);
// First and last (inclusive) item that can be on screen; first > last when none.
void visible_range(const ViewMetrics& m, int scroll_x, int scroll_y, int* first, int* last);
// The item at a point of the content area, or -1.
int index_at(const ViewMetrics& m, int x, int y, int scroll_x, int scroll_y);
// Items whose cell touches the rectangle (rubber band selection).
std::vector<int> indices_in_rect(const ViewMetrics& m, int x0, int y0, int x1, int y1, int scroll_x, int scroll_y);
// New scroll offset so that item `index` is fully visible.
void scroll_to_show(const ViewMetrics& m, int index, int* scroll_x, int* scroll_y);
int max_scroll_x(const ViewMetrics& m);
int max_scroll_y(const ViewMetrics& m);

enum class Nav { Up, Down, Left, Right, PageUp, PageDown, Home, End };
// Where the arrow keys move the focus; stays put at the edges.
int navigate(const ViewMetrics& m, int from, Nav key);

}  // namespace fleetwm::fm
