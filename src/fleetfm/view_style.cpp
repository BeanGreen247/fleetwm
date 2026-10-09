#include "view_style.hpp"

#include <algorithm>
#include <cctype>

namespace fleetwm::fm {

namespace {
using TK = ToolbarKind;
using NK = NavKind;
using AK = AddressKind;
using SP = SelectionPaint;
using IS = IconStyle;
using VM = ViewMode;
}  // namespace

const std::vector<StyleSpec>& all_styles() {
  // id, key, name, toolbar, nav, address, addr_in_toolbar, menu, details_pane, status, search_visible, tabs_below, header, light,
  // selection, icons, default view, toolbar_h, address_h, nav_w, row_h, header_h, status_h, tab_h, details_h, font, icon, corner
  static const std::vector<StyleSpec> list = {
      {ViewStyle::Windows7, "windows7", "Windows 7", TK::CommandBar, NK::Tree, AK::Breadcrumb, true, false, true, false, true, true, true, true,
       SP::Win7Glass, IS::Aero, VM::Details, 30, 28, 190, 22, 24, 22, 26, 62, 13, 16, 3},
      {ViewStyle::Windows10, "windows10", "Windows 10", TK::Ribbon, NK::Tree, AK::Breadcrumb, false, false, false, true, true, true, true, true,
       SP::Flat, IS::Flat, VM::Details, 30, 32, 200, 24, 26, 24, 30, 0, 13, 16, 0},
      {ViewStyle::Mac, "mac", "Mac Finder", TK::Finder, NK::Sidebar, AK::PathEntry, false, false, false, true, false, true, true, true,
       SP::RoundedAccent, IS::Finder, VM::MediumIcons, 40, 0, 180, 24, 24, 24, 28, 0, 13, 48, 6},
      {ViewStyle::Caja, "caja", "Caja (MATE)", TK::Icons, NK::Sidebar, AK::PathEntry, false, true, false, true, true, true, true, true,
       SP::Flat, IS::Flat, VM::MediumIcons, 36, 32, 190, 24, 24, 22, 26, 0, 12, 48, 2},
      {ViewStyle::Nautilus, "nautilus", "Nautilus (GNOME)", TK::HeaderBar, NK::Sidebar, AK::Breadcrumb, true, false, false, false, false, false, true, true,
       SP::RoundedAccent, IS::Flat, VM::MediumIcons, 46, 32, 220, 36, 30, 0, 34, 0, 14, 64, 8},
      {ViewStyle::Nemo, "nemo", "Nemo (Cinnamon)", TK::Icons, NK::Sidebar, AK::Breadcrumb, false, true, false, true, true, true, true, true,
       SP::Flat, IS::Flat, VM::MediumIcons, 36, 32, 200, 26, 24, 22, 26, 0, 12, 48, 3},
      {ViewStyle::Thunar, "thunar", "Thunar (Xfce)", TK::Icons, NK::Sidebar, AK::Breadcrumb, false, true, false, true, true, true, true, true,
       SP::Flat, IS::Flat, VM::MediumIcons, 34, 30, 180, 24, 22, 22, 26, 0, 12, 48, 2},
      {ViewStyle::PcManFm, "pcmanfm", "PCManFM (LXDE)", TK::Compact, NK::Sidebar, AK::PathEntry, false, true, false, true, false, true, true, true,
       SP::Outline, IS::Flat, VM::MediumIcons, 30, 28, 170, 22, 22, 20, 24, 0, 12, 32, 0},
      {ViewStyle::Dolphin, "dolphin", "Dolphin (KDE)", TK::Icons, NK::Sidebar, AK::Breadcrumb, false, false, false, true, false, true, true, true,
       SP::Flat, IS::Flat, VM::Details, 36, 32, 200, 26, 26, 24, 28, 0, 13, 22, 4},
  };
  return list;
}

const StyleSpec& style_spec(ViewStyle s) {
  for (const StyleSpec& sp : all_styles())
    if (sp.id == s) return sp;
  return all_styles().front();
}

static std::string squash(std::string_view s) {
  std::string o;
  for (char c : s)
    if (std::isalnum(static_cast<unsigned char>(c))) o.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return o;
}

bool parse_style(std::string_view name, ViewStyle* out) {
  const std::string n = squash(name);
  if (n.empty()) return false;
  for (const StyleSpec& sp : all_styles())
    if (n == squash(sp.key) || n == squash(sp.name)) {
      *out = sp.id;
      return true;
    }
  static const struct {
    const char* alias;
    ViewStyle s;
  } aliases[] = {{"win7", ViewStyle::Windows7}, {"windows7explorer", ViewStyle::Windows7}, {"win10", ViewStyle::Windows10},
                 {"windows11", ViewStyle::Windows10}, {"finder", ViewStyle::Mac}, {"macos", ViewStyle::Mac}, {"mate", ViewStyle::Caja},
                 {"gnome", ViewStyle::Nautilus}, {"files", ViewStyle::Nautilus}, {"cinnamon", ViewStyle::Nemo}, {"xfce", ViewStyle::Thunar},
                 {"lxde", ViewStyle::PcManFm}, {"lxqt", ViewStyle::PcManFm}, {"kde", ViewStyle::Dolphin}, {"plasma", ViewStyle::Dolphin}};
  for (const auto& a : aliases)
    if (n == a.alias) {
      *out = a.s;
      return true;
    }
  return false;
}

const char* style_key(ViewStyle s) { return style_spec(s).key; }

const char* view_mode_key(ViewMode m) {
  switch (m) {
    case ViewMode::Details: return "details";
    case ViewMode::List: return "list";
    case ViewMode::SmallIcons: return "small_icons";
    case ViewMode::MediumIcons: return "medium_icons";
    case ViewMode::LargeIcons: return "large_icons";
    case ViewMode::ExtraLargeIcons: return "extra_large_icons";
    case ViewMode::Tiles: return "tiles";
    case ViewMode::Content: return "content";
  }
  return "details";
}

bool parse_view_mode(std::string_view name, ViewMode* out) {
  const std::string n = squash(name);
  for (ViewMode m : {ViewMode::Details, ViewMode::List, ViewMode::SmallIcons, ViewMode::MediumIcons, ViewMode::LargeIcons, ViewMode::ExtraLargeIcons, ViewMode::Tiles, ViewMode::Content})
    if (n == squash(view_mode_key(m))) {
      *out = m;
      return true;
    }
  return false;
}

int view_mode_icon_px(ViewMode m) {
  switch (m) {
    case ViewMode::SmallIcons: return 24;
    case ViewMode::MediumIcons: return 48;
    case ViewMode::LargeIcons: return 96;
    case ViewMode::ExtraLargeIcons: return 256;
    case ViewMode::Tiles: return 48;
    case ViewMode::Content: return 32;
    default: return 16;
  }
}

Layout compute_layout(const StyleSpec& s, const LayoutInput& in) {
  Layout l;
  int y = 0;
  const int W = in.width, H = in.height;
  if (in.menu_bar) {
    l.menu = {0, y, W, 24};
    y += 24;
  }
  // Top rows. Windows 7: one row holding back / forward, the breadcrumb address and the search box, then the command bar.
  // The styles with a separate address row (Caja, Nemo, Thunar ...) put the toolbar first and the location below it.
  l.back_forward_w = s.toolbar == ToolbarKind::HeaderBar || s.toolbar == ToolbarKind::Finder ? 76 : (s.toolbar == ToolbarKind::Ribbon ? 112 : 84);
  l.search_w = std::clamp(W / 5, 120, 220);
  const bool search_shown = s.search_box_visible || in.search_open;
  if (s.toolbar == ToolbarKind::HeaderBar) {
    // one bar: back / forward, the location in the middle, search, view and menu buttons at the right
    const int tools_w = 128;
    l.toolbar = {0, y, W, s.toolbar_h};
    const int ah = 32;
    const int aw = W - l.back_forward_w - tools_w - 12 - (search_shown ? l.search_w + 8 : 0);
    l.address = {l.back_forward_w + 4, y + (s.toolbar_h - ah) / 2, std::max(0, aw), ah};
    y += s.toolbar_h;
  } else if (s.address_in_toolbar_row) {
    const int row = std::max(s.address_h, s.toolbar_h > 36 ? s.toolbar_h : 36);
    l.address = {l.back_forward_w, y + (row - s.address_h) / 2, W - l.back_forward_w - (search_shown ? l.search_w + 8 : 40) - 8, s.address_h};
    l.toolbar = {0, y + row, W, s.toolbar_h};
    y += row + s.toolbar_h;
  } else if (s.address_h > 0) {
    l.toolbar = {0, y, W, s.toolbar_h};
    y += s.toolbar_h;
    l.address = {l.back_forward_w, y + 2, W - l.back_forward_w - (search_shown ? l.search_w + 8 : 8) - 8, s.address_h};
    y += s.address_h + 4;
  } else {  // Finder: back, title, view switcher, search all in one bar
    l.toolbar = {0, y, W, s.toolbar_h};
    l.address = {l.back_forward_w, y, 0, 0};
    y += s.toolbar_h;
  }
  if (in.tabs) {
    l.tabs = {0, y, W, s.tab_h};
    y += s.tab_h;
  }
  int bottom = H;
  if (in.status_bar) {
    bottom -= s.status_h;
    l.status = {0, bottom, W, s.status_h};
  }
  const bool narrow = W < 1000, short_win = H < 700;
  if (in.details_pane && !short_win) {
    bottom -= s.details_h;
    l.details = {0, bottom, W, s.details_h};
  }
  int x = 0;
  if (in.nav_pane && s.nav != NavKind::None && !(narrow && W < 760)) {
    const int nw = in.nav_width_override > 0 ? in.nav_width_override : s.nav_w;
    const int cap = std::max(120, W / 3);  // never more than a third of the window
    l.nav = {0, y, std::min(nw, cap), bottom - y};
    x = l.nav.w;
  }
  int right = W;
  if (in.preview && W >= 900) {  // too narrow a window keeps the list instead
    const int pw = std::clamp(W / 3, 240, 360);
    l.preview = {W - pw, y, pw, std::max(0, bottom - y)};
    right = W - pw;
  }
  l.header = {x, y, right - x, s.header_row ? s.header_h : 0};
  l.content = {x, y, right - x, std::max(0, bottom - y)};
  l.fits = l.content.w >= 400 && l.content.h >= 160 && (s.address_h == 0 || l.address.w >= 120);
  return l;
}

}  // namespace fleetwm::fm
