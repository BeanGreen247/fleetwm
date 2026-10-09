#pragma once

// What the Desktop taskbar shows and in which order, and the one-dimensional layout both taskbar orientations share: the horizontal
// bar lays elements out along x, the vertical one along y. Pure arithmetic and names; the bar draws, Settings edits the lists.
//
// Two zones: the main zone (start button, workspaces, pinned apps, the window list) fills from the start edge, the status zone
// (tray, keyboard layout, volume, network, Bluetooth, power mode, battery, metrics, clock) fills from the far edge. The window list
// takes whatever room is left between them. Inside a zone the order is the user's.

#include <string>
#include <vector>

namespace fleetwm {

enum class TbElement { Start, Workspaces, Pinned, Windows, Metrics, Tray, Layout, Volume, Network, Bluetooth, Mode, Battery, Clock };
inline constexpr int kTbElementCount = 13;

const char* tb_element_name(TbElement e);   // the name saved in bar.toml: "start", "workspaces", ...
const char* tb_element_label(TbElement e);  // what Settings shows: "Start button", "Workspaces", ...
bool tb_element_from_name(const std::string& name, TbElement* out);
bool tb_is_main_zone(TbElement e);  // start, workspaces, pinned, windows

std::vector<TbElement> tb_default_order();
// A saved order with unknown names and repeats dropped and every missing element added after its predecessor in the default order
// (so a file written by an older version, without "bluetooth", still loads and the new element lands next to the network icon).
std::vector<TbElement> tb_normalize_order(const std::vector<std::string>& saved);
// Unknown names and repeats dropped.
std::vector<TbElement> tb_normalize_hidden(const std::vector<std::string>& saved);
std::vector<std::string> tb_names(const std::vector<TbElement>& list);

// Moves `e` one place towards the front (delta < 0) or the back (delta > 0) among the elements of its own zone. Returns false at the edge.
bool tb_move(std::vector<TbElement>* order, TbElement e, int delta);

struct TbItem {
  TbElement id;
  double size;  // along the axis; for Windows: the room all its buttons would like (the layout may give less)
};
struct TbSlot {
  TbElement id;
  double pos, size;
};

struct TbLayoutParams {
  double length = 0;           // the bar's length along the axis
  double margin = 6;           // free space at both ends
  double gap = 10;             // between neighbouring elements
  double min_windows = 44;     // the window list is dropped when it would get less than this
  bool centered_start = false; // Windows 11 style: start, workspaces, pinned apps and the window buttons as one group in the middle
};

// Lays out the visible elements (given in the user's order). Main-zone items fill from the start, status-zone items from the end. The
// window list gets the rest, or with centered_start just what it wants and the whole main group is centred (but never over the status zone).
std::vector<TbSlot> tb_layout(const std::vector<TbItem>& visible, const TbLayoutParams& p);

}  // namespace fleetwm
