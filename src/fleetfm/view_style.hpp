#pragma once

#include <string>
#include <string_view>
#include <vector>

// The look and behaviour presets: Windows 7 (the default), Windows 10, Mac Finder, and the Linux file managers Caja (MATE),
// Nautilus (GNOME), Nemo (Cinnamon), Thunar (Xfce), PCManFM (LXDE) and Dolphin (KDE). A style is data: which bars exist,
// where the address bar sits, how tall a row is, how a selection is painted. The window reads it; nothing branches on a
// style name elsewhere, so a user setting can override any single value.

namespace fleetwm::fm {

enum class ViewStyle { Windows7, Windows10, Mac, Caja, Nautilus, Nemo, Thunar, PcManFm, Dolphin };

enum class ToolbarKind { CommandBar, Ribbon, Icons, HeaderBar, Finder, Compact };
enum class NavKind { Tree, Sidebar, None };
enum class AddressKind { Breadcrumb, PathEntry };
enum class SelectionPaint { Win7Glass, Flat, RoundedAccent, Outline };
enum class IconStyle { Aero, Flat, Finder };
enum class ViewMode { Details, List, SmallIcons, MediumIcons, LargeIcons, ExtraLargeIcons, Tiles, Content };

struct StyleSpec {
  ViewStyle id;
  const char* key;       // "windows7"
  const char* name;      // "Windows 7"
  ToolbarKind toolbar;
  NavKind nav;
  AddressKind address;
  bool address_in_toolbar_row;   // Windows 7: back / forward and the address bar share the top row
  bool menu_bar;                  // File Edit View ... always shown
  bool details_pane;              // the strip under the list (Windows 7)
  bool status_bar;
  bool search_box_visible;        // false: a magnifier that opens it (Nautilus, Finder)
  bool tabs_below_toolbar;        // tab strip directly above the file list
  bool header_row;                // column headings in details view
  bool light;                     // the style is drawn on light colours
  SelectionPaint selection;
  IconStyle icons;
  ViewMode default_view;
  int toolbar_h, address_h, nav_w, row_h, header_h, status_h, tab_h, details_h, font_px, icon_px, corner;
};

const std::vector<StyleSpec>& all_styles();
const StyleSpec& style_spec(ViewStyle s);
// "windows7", "Windows 10", "caja" ... any case, spaces, dashes and underscores ignored; false when unknown.
bool parse_style(std::string_view name, ViewStyle* out);
const char* style_key(ViewStyle s);

const char* view_mode_key(ViewMode m);
bool parse_view_mode(std::string_view name, ViewMode* out);
// Icon edge in px for the icon-based modes (16/32/48/96/256), 16 for details and list.
int view_mode_icon_px(ViewMode m);

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  bool operator==(const Rect&) const = default;
};

struct LayoutInput {
  int width = 0, height = 0;
  bool nav_pane = true, details_pane = true, status_bar = false, menu_bar = false, tabs = true;  // the window passes the user's choices
  bool preview = false;        // the preview pane at the right of the file list
  bool search_open = false;    // the search box was opened (styles that hide it until asked)
  int nav_width_override = 0;  // 0 = the style's width (the user dragged the splitter otherwise)
};
struct Layout {
  Rect menu, toolbar, address, nav, tabs, header, content, details, status, preview;
  int back_forward_w = 0;      // space taken by the back / forward buttons in the top row
  int search_w = 0;
  bool fits = true;            // the content area keeps a usable size
};
// Rectangles for every part of the window. At 1024x768 and above `fits` holds for every style; below it the navigation pane
// collapses first, then the details pane.
Layout compute_layout(const StyleSpec& s, const LayoutInput& in);

}  // namespace fleetwm::fm
