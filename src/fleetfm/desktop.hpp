#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "dir_listing.hpp"
#include "icons.hpp"

// The desktop of the Desktop layout: the icons of ~/Desktop plus Computer, Home and Trash, in a grid from the top left like Windows 7,
// with the options of its right-click menu (icon size, auto arrange, align to grid, show icons, sort). Pure data and arithmetic: the
// program `fleetwm-desktop` draws it and the unit tests drive it.

namespace fleetwm::fm {

enum class DesktopIconSize { Small, Medium, Large };

struct DesktopConfig {
  bool show_icons = true;
  DesktopIconSize icon_size = DesktopIconSize::Medium;
  SortKey sort_key = SortKey::Name;
  bool ascending = true;
  bool auto_arrange = true;     // icons fill the grid in order; off, they stay where they were dragged
  bool align_to_grid = true;
  bool show_hint = true;        // the little "press Super+/ for shortcuts" card in the Tiling layout
  bool show_computer = true, show_home = true, show_trash = true;
  std::map<std::string, std::pair<int, int>> cells;  // manual positions: icon key -> (column, row)
  bool operator==(const DesktopConfig&) const = default;
};

std::string desktop_config_path();
DesktopConfig load_desktop_config();
void save_desktop_config(const DesktopConfig& c);  // throws std::runtime_error on I/O failure
int desktop_icon_px(DesktopIconSize s);            // 32, 48, 64

struct DesktopIcon {
  std::string key;      // stable id: "computer", "home", "trash" or the file name
  std::string label;
  std::string target;   // a path, or computer:/// and trash:/// for the special ones
  IconKind icon = IconKind::File;
  bool special = false, is_dir = false;
  int col = 0, row = 0;  // grid cell once placed
  int64_t mtime = 0;
  uint64_t size = 0;
};

// Special icons first (Computer, Home, Trash), then the contents of `desktop_dir` (hidden files left out, ".desktop" launchers named by their Name=
// when they have one), sorted by the configuration.
std::vector<DesktopIcon> list_desktop_icons(const std::string& desktop_dir, const std::string& home, const DesktopConfig& c);

struct DesktopGrid {
  int cell_w = 0, cell_h = 0, cols = 1, rows = 1, margin_x = 8, margin_y = 8, icon_px = 48;
};
DesktopGrid desktop_grid(int width, int height, DesktopIconSize s);
// Fills col/row of every icon: in order down the columns when auto arranging, else from the saved cells (an icon without one, or whose cell is
// taken or outside the screen, goes to the first free cell).
void place_desktop_icons(std::vector<DesktopIcon>* icons, const DesktopGrid& g, const DesktopConfig& c);
struct Pt {
  int x, y;
};
Pt desktop_cell_origin(const DesktopGrid& g, int col, int row);
int desktop_icon_at(const std::vector<DesktopIcon>& icons, const DesktopGrid& g, int x, int y);
std::vector<int> desktop_icons_in_rect(const std::vector<DesktopIcon>& icons, const DesktopGrid& g, int x0, int y0, int x1, int y1);
// The cell nearest to a point (dragging an icon with auto arrange off).
std::pair<int, int> desktop_cell_at(const DesktopGrid& g, int x, int y);
// Records that `key` now lives in this cell (swapping nothing: a taken cell is refused). True when it moved.
bool move_desktop_icon(const std::vector<DesktopIcon>& icons, const DesktopGrid& g, DesktopConfig* c, const std::string& key, int col, int row);

// "Super+/" -> {"Super", "/"}: the caps shown on the hint card.
std::vector<std::string> split_key_combo(const std::string& combo);

}  // namespace fleetwm::fm
