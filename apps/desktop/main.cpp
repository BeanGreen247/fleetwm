// fleetwm-desktop. Desktop layout: the icons of ~/Desktop (plus Computer, Home, Trash) on a layer surface above the wallpaper and under every
// window, and the right-click menu of Windows 7 (View, Sort by, Refresh, Paste, New, Display settings, Personalize), with "Show desktop icons"
// to hide them. Tiling layout: no icons, no menu (they go away by themselves when the layout changes); a small card in the corner shows the
// keys that open the shortcut list.

#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "desktop.hpp"
#include "file_ops.hpp"
#include "fleetkit.hpp"
#include "keybinds_config.hpp"
#include "line_edit.hpp"
#include "malloc_tuning.hpp"
#include "quit_signals.hpp"
#include "shortcut_list.hpp"
#include "theme.hpp"
#include "transfer.hpp"
#include "trash.hpp"
#include "version.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

namespace {

using namespace fleetwm;
using namespace fleetwm::fm;
using kit::Color;
namespace fs = std::filesystem;

double now_s() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// ---- the popup menu, drawn like a Windows 7 menu ---------------------------------------------------------------------------------------
struct MenuEntry {
  std::string label, id;
  bool enabled = true, checked = false, radio = false, separator = false, bold = false;
  std::vector<MenuEntry> sub;
};
MenuEntry sep() {
  MenuEntry m;
  m.separator = true;
  return m;
}
MenuEntry entry(std::string label, std::string id, bool enabled = true, bool checked = false, bool radio = false) {
  MenuEntry m;
  m.label = std::move(label);
  m.id = std::move(id);
  m.enabled = enabled;
  m.checked = checked;
  m.radio = radio;
  return m;
}
MenuEntry submenu(std::string label, std::vector<MenuEntry> sub) {
  MenuEntry m;
  m.label = std::move(label);
  m.sub = std::move(sub);
  return m;
}

constexpr double kRow = 25, kPx = 13;

struct Level {
  std::vector<MenuEntry> items;
  double x = 0, y = 0, w = 0, h = 0;
  int hot = -1;
  std::vector<double> row_y;  // top of each row, relative to y
};

struct Desktop {
  kit::App app;
  std::unique_ptr<kit::Surface> surface, hint;
  DesktopConfig cfg;
  bool desktop_mode = false;
  std::string desktop_dir, home;
  std::vector<DesktopIcon> icons;
  DesktopGrid grid;
  std::set<std::string> selected;
  int hover = -1;
  IconCache cache{64};
  int width = 0, height = 0;

  // pointer
  double mx = 0, my = 0, press_x = 0, press_y = 0;
  bool down = false, band = false, dragging = false;
  std::string drag_key;
  double last_click = 0;
  int last_click_icon = -1;

  // menu
  std::vector<Level> menu;  // [0] the menu, [1] its open submenu
  std::string menu_icon_key;

  // rename
  std::string rename_key;
  LineEdit edit;
  std::string select_after_reload;
  bool rename_after_reload = false;

  Trash trash;
  std::vector<std::string> pending_paste;

  // ---- loading ----
  void reload() {
    cfg = load_desktop_config();
    const char* h = std::getenv("HOME");
    home = h && *h ? h : "/";
    const char* xdg = std::getenv("XDG_DESKTOP_DIR");
    desktop_dir = xdg && *xdg ? xdg : home + "/Desktop";
    icons = list_desktop_icons(desktop_dir, home, cfg);
    relayout();
    if (!select_after_reload.empty()) {
      selected.clear();
      for (const DesktopIcon& i : icons)
        if (i.key == select_after_reload) {
          selected.insert(i.key);
          if (rename_after_reload) begin_rename(i.key);
        }
      select_after_reload.clear();
      rename_after_reload = false;
    }
    // drop selections that no longer exist
    std::set<std::string> keep;
    for (const DesktopIcon& i : icons)
      if (selected.count(i.key)) keep.insert(i.key);
    selected = keep;
    redraw();
  }
  void relayout() {
    grid = desktop_grid(std::max(1, width), std::max(1, height), cfg.icon_size);
    place_desktop_icons(&icons, grid, cfg);
  }
  void redraw() {
    if (surface) surface->queue_draw();
  }
  void save() {
    try {
      save_desktop_config(cfg);
    } catch (const std::exception&) {
    }
  }

  // ---- layout mode: surfaces come and go with the window layout ----
  void apply_mode() {
    const bool want_desktop = load_theme_config().window_layout == WindowLayout::Desktop;
    if (want_desktop != desktop_mode || (!surface && !hint)) {
      desktop_mode = want_desktop;
      surface.reset();
      hint.reset();
      menu.clear();
      if (desktop_mode) make_desktop_surface();
      else if (cfg.show_hint) make_hint_surface();
    } else if (!desktop_mode && !hint && cfg.show_hint) {
      make_hint_surface();
    } else if (!desktop_mode && hint && !cfg.show_hint) {
      hint.reset();
    }
  }
  void make_desktop_surface() {
    kit::Surface::Config c;
    c.layer = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
    c.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    c.width = c.height = 0;
    c.exclusive_zone = 0;
    c.keyboard_mode = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND;
    c.name = "fleetwm-desktop";
    c.output = app.requested_output();
    surface = std::make_unique<kit::Surface>(app, c);
    surface->on_draw = [this](cairo_t* cr, int w, int h) { draw(cr, w, h); };
    surface->on_configure = [this](int w, int h) {
      width = w;
      height = h;
      relayout();
    };
    surface->on_motion = [this](double x, double y) { motion(x, y); };
    surface->on_leave = [this] {
      hover = -1;
      redraw();
    };
    surface->on_button = [this](double x, double y, uint32_t b, bool p) { button(x, y, b, p); };
    surface->on_key = [this](const kit::KeyEvent& e) { key(e); };
    surface->on_keyboard_leave = [this] {
      if (!rename_key.empty()) commit_rename();
      close_menu();
    };
  }
  void make_hint_surface() {
    const KeybindsConfig kb = load_keybinds_config();
    caps = split_key_combo(format_key_combo(kb.shortcuts_help));
    // measure with a scratch surface
    cairo_surface_t* tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* cr = cairo_create(tmp);
    double w = 20 + kit::measure_text(cr, "Shortcuts:", kPx).width + 8;
    for (const std::string& c : caps) w += kit::measure_text(cr, c, kPx, true).width + 18 + 6;
    cairo_destroy(cr);
    cairo_surface_destroy(tmp);
    kit::Surface::Config c;
    c.layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
    c.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    c.width = static_cast<int>(std::ceil(w)) + 8;
    c.height = 40;
    c.margin_right = 16;
    c.margin_bottom = 16;
    c.exclusive_zone = 0;
    c.keyboard_mode = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
    c.name = "fleetwm-shortcut-hint";
    c.output = app.requested_output();
    hint = std::make_unique<kit::Surface>(app, c);
    hint->set_input_passthrough();
    hint->on_draw = [this](cairo_t* cr, int ww, int hh) { draw_hint(cr, ww, hh); };
    hint->queue_draw();
  }
  std::vector<std::string> caps;

  void draw_hint(cairo_t* cr, int w, int h) {
    kit::rounded_rect(cr, 2, 2, w - 4, h - 4, (h - 4) / 2.0);
    cairo_set_source_rgba(cr, 0.07, 0.08, 0.11, 0.80);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    double x = 20;
    x += kit::draw_text(cr, "Shortcuts:", x, h / 2.0 + 4.5, kPx, {0.82, 0.85, 0.92, 1}) + 8;
    for (const std::string& c : caps) {
      const double tw = kit::measure_text(cr, c, kPx, true).width;
      kit::rounded_rect(cr, x, h / 2.0 - 11, tw + 18, 22, 5);
      cairo_set_source_rgba(cr, 0.93, 0.95, 1, 0.95);
      cairo_fill(cr);
      kit::rounded_rect(cr, x + 0.5, h / 2.0 - 10.5, tw + 17, 21, 5);
      cairo_set_source_rgba(cr, 0.55, 0.6, 0.72, 1);
      cairo_stroke(cr);
      kit::draw_text(cr, c, x + 9, h / 2.0 + 4.5, kPx, {0.1, 0.12, 0.2, 1}, true);
      x += tw + 18 + 6;
    }
  }

  // ---- drawing ----
  void draw(cairo_t* cr, int w, int h) {
    width = w;
    height = h;
    if (grid.cols <= 0 || grid.cell_w != desktop_grid(w, h, cfg.icon_size).cell_w || grid.cols != desktop_grid(w, h, cfg.icon_size).cols || grid.rows != desktop_grid(w, h, cfg.icon_size).rows) relayout();
    if (cfg.show_icons) {
      for (size_t k = 0; k < icons.size(); ++k) draw_icon(cr, static_cast<int>(k));
    }
    if (band) {
      const double x0 = std::min(press_x, mx), y0 = std::min(press_y, my), w2 = std::abs(mx - press_x), h2 = std::abs(my - press_y);
      cairo_rectangle(cr, x0 + 0.5, y0 + 0.5, w2, h2);
      cairo_set_source_rgba(cr, 0.35, 0.6, 1, 0.25);
      cairo_fill_preserve(cr);
      cairo_set_source_rgba(cr, 0.55, 0.75, 1, 0.9);
      cairo_set_line_width(cr, 1);
      cairo_stroke(cr);
    }
    for (size_t i = 0; i < menu.size(); ++i) draw_level(cr, menu[i], i == 0 ? -1 : menu[0].hot);
  }
  void draw_icon(cairo_t* cr, int k) {
    const DesktopIcon& ic = icons[k];
    Pt o = desktop_cell_origin(grid, ic.col, ic.row);
    if (dragging && ic.key == drag_key) {
      o.x += static_cast<int>(mx - press_x);
      o.y += static_cast<int>(my - press_y);
    }
    const bool sel = selected.count(ic.key) != 0, hot = k == hover && !down;
    if (sel || hot) {
      kit::rounded_rect(cr, o.x + 3, o.y + 1, grid.cell_w - 6, grid.cell_h - 2, 4);
      cairo_set_source_rgba(cr, 0.35, 0.6, 1, sel ? 0.34 : 0.18);
      cairo_fill_preserve(cr);
      cairo_set_source_rgba(cr, 0.65, 0.82, 1, sel ? 0.85 : 0.5);
      cairo_set_line_width(cr, 1);
      cairo_stroke(cr);
    }
    cairo_surface_t* s = cache.get(ic.icon, grid.icon_px);
    cairo_set_source_surface(cr, s, std::round(o.x + (grid.cell_w - grid.icon_px) / 2.0), o.y + 4);
    cairo_paint(cr);
    const double px = 12;
    const double ty = o.y + 4 + grid.icon_px + 6;
    if (ic.key == rename_key) {
      kit::rounded_rect(cr, o.x + 2, ty - 1, grid.cell_w - 4, 18, 2);
      cairo_set_source_rgba(cr, 1, 1, 1, 0.97);
      cairo_fill(cr);
      if (edit.has_selection()) {
        const double a = kit::measure_text(cr, edit.text.substr(0, edit.sel_begin()), px).width, b = kit::measure_text(cr, edit.text.substr(0, edit.sel_end()), px).width;
        cairo_rectangle(cr, o.x + 6 + a, ty + 1, b - a, 14);
        cairo_set_source_rgba(cr, 0.2, 0.5, 0.95, 1);
        cairo_fill(cr);
      }
      kit::draw_text(cr, edit.text, o.x + 6, ty + 12, px, {0, 0, 0, 1});
      const double cx = o.x + 6 + kit::measure_text(cr, edit.text.substr(0, edit.caret), px).width;
      cairo_move_to(cr, std::round(cx) + 0.5, ty + 1);
      cairo_line_to(cr, std::round(cx) + 0.5, ty + 15);
      cairo_set_source_rgba(cr, 0, 0, 0, 1);
      cairo_stroke(cr);
      return;
    }
    // two lines, centred, white with a shadow so it reads on any wallpaper
    std::string l1 = ic.label, l2;
    const double maxw = grid.cell_w - 10;
    if (kit::measure_text(cr, l1, px).width > maxw) {
      size_t cut = l1.size();
      while (cut > 1 && kit::measure_text(cr, l1.substr(0, cut), px).width > maxw) {
        --cut;
        while (cut > 1 && (static_cast<unsigned char>(l1[cut]) & 0xC0) == 0x80) --cut;
      }
      l2 = l1.substr(cut);
      l1 = l1.substr(0, cut);
      if (kit::measure_text(cr, l2, px).width > maxw) {
        size_t c2 = l2.size();
        while (c2 > 1 && kit::measure_text(cr, l2.substr(0, c2) + "\xE2\x80\xA6", px).width > maxw) {
          --c2;
          while (c2 > 1 && (static_cast<unsigned char>(l2[c2]) & 0xC0) == 0x80) --c2;
        }
        l2 = l2.substr(0, c2) + "\xE2\x80\xA6";
      }
    }
    auto line = [&](const std::string& t, double y) {
      const double tw = kit::measure_text(cr, t, px).width, x = o.x + (grid.cell_w - tw) / 2;
      kit::draw_text(cr, t, x + 1, y + 1, px, {0, 0, 0, 0.7});
      kit::draw_text(cr, t, x, y, px, {1, 1, 1, 1});
    };
    line(l1, ty + 11);
    if (!l2.empty()) line(l2, ty + 25);
  }

  // ---- menu model and drawing ----
  void open_menu_at(std::vector<MenuEntry> items, double x, double y) {
    menu.clear();
    menu.push_back(measure(std::move(items), x, y));
    redraw();
  }
  Level measure(std::vector<MenuEntry> items, double x, double y) {
    Level L;
    cairo_surface_t* tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* cr = cairo_create(tmp);
    double w = 190, h = 8;
    for (const MenuEntry& m : items) {
      L.row_y.push_back(h - 4);
      if (m.separator) {
        h += 9;
        continue;
      }
      w = std::max(w, kit::measure_text(cr, m.label, kPx, m.bold).width + 70);
      h += kRow;
    }
    cairo_destroy(cr);
    cairo_surface_destroy(tmp);
    L.items = std::move(items);
    L.w = w;
    L.h = h;
    L.x = std::max(2.0, std::min(x, width - w - 4));
    L.y = std::max(2.0, std::min(y, height - h - 4));
    return L;
  }
  void draw_level(cairo_t* cr, Level& L, int) {
    kit::rounded_rect(cr, L.x + 3, L.y + 3, L.w, L.h, 3);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.28);
    cairo_fill(cr);
    kit::rounded_rect(cr, L.x, L.y, L.w, L.h, 3);
    cairo_set_source_rgb(cr, 0.96, 0.97, 0.98);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.59, 0.59, 0.59);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    cairo_rectangle(cr, L.x + 1, L.y + 1, 26, L.h - 2);  // the icon gutter
    cairo_set_source_rgb(cr, 0.93, 0.95, 0.97);
    cairo_fill(cr);
    cairo_move_to(cr, L.x + 27.5, L.y + 2);
    cairo_line_to(cr, L.x + 27.5, L.y + L.h - 2);
    cairo_set_source_rgb(cr, 0.84, 0.87, 0.91);
    cairo_stroke(cr);
    double y = L.y + 4;
    for (size_t i = 0; i < L.items.size(); ++i) {
      const MenuEntry& m = L.items[i];
      if (m.separator) {
        cairo_move_to(cr, L.x + 32, y + 4.5);
        cairo_line_to(cr, L.x + L.w - 6, y + 4.5);
        cairo_set_source_rgb(cr, 0.84, 0.87, 0.91);
        cairo_stroke(cr);
        y += 9;
        continue;
      }
      const bool hot = static_cast<int>(i) == L.hot && m.enabled;
      if (hot) {
        kit::rounded_rect(cr, L.x + 3, y, L.w - 6, kRow, 2);
        cairo_pattern_t* p = cairo_pattern_create_linear(0, y, 0, y + kRow);
        cairo_pattern_add_color_stop_rgb(p, 0, 0.95, 0.97, 1.0);
        cairo_pattern_add_color_stop_rgb(p, 1, 0.82, 0.90, 0.99);
        cairo_set_source(cr, p);
        cairo_fill_preserve(cr);
        cairo_pattern_destroy(p);
        cairo_set_source_rgb(cr, 0.72, 0.84, 0.98);
        cairo_stroke(cr);
      }
      const Color tc = m.enabled ? Color{0, 0, 0, 1} : Color{0.6, 0.6, 0.6, 1};
      if (m.checked) {
        if (m.radio) {
          cairo_arc(cr, L.x + 16, y + kRow / 2, 3.5, 0, 2 * M_PI);
          kit::set_source(cr, tc);
          cairo_fill(cr);
        } else {
          cairo_move_to(cr, L.x + 9, y + 13);
          cairo_line_to(cr, L.x + 13, y + 17);
          cairo_line_to(cr, L.x + 21, y + 8);
          kit::set_source(cr, tc);
          cairo_set_line_width(cr, 1.8);
          cairo_stroke(cr);
        }
      }
      kit::draw_text(cr, m.label, L.x + 34, y + kRow / 2 + 4.5, kPx, tc, m.bold);
      if (!m.sub.empty()) {
        cairo_move_to(cr, L.x + L.w - 16, y + 8);
        cairo_line_to(cr, L.x + L.w - 11, y + 12.5);
        cairo_line_to(cr, L.x + L.w - 16, y + 17);
        cairo_close_path(cr);
        kit::set_source(cr, tc);
        cairo_fill(cr);
      }
      y += kRow;
    }
  }
  int level_item_at(const Level& L, double x, double y) const {
    if (x < L.x || x >= L.x + L.w || y < L.y || y >= L.y + L.h) return -2;
    double cy = L.y + 4;
    for (size_t i = 0; i < L.items.size(); ++i) {
      const double hgt = L.items[i].separator ? 9 : kRow;
      if (y >= cy && y < cy + hgt) return L.items[i].separator ? -1 : static_cast<int>(i);
      cy += hgt;
    }
    return -1;
  }
  void close_menu() {
    if (!menu.empty()) {
      menu.clear();
      redraw();
    }
  }
  void hover_menu(double x, double y) {
    for (int lv = static_cast<int>(menu.size()) - 1; lv >= 0; --lv) {
      const int it = level_item_at(menu[lv], x, y);
      if (it == -2) continue;
      menu[lv].hot = it;
      if (lv == 0) {
        menu.resize(1);
        if (it >= 0 && !menu[0].items[it].sub.empty() && menu[0].items[it].enabled) {
          Level s = measure(menu[0].items[it].sub, menu[0].x + menu[0].w - 2, menu[0].y + 4 + menu[0].row_y[it] + 4 - 4);
          if (s.x < menu[0].x + menu[0].w - 4) s.x = std::max(2.0, menu[0].x - s.w + 2);  // no room at the right: open to the left
          menu.push_back(std::move(s));
        }
      }
      redraw();
      return;
    }
  }
  bool click_menu(double x, double y) {
    for (int lv = static_cast<int>(menu.size()) - 1; lv >= 0; --lv) {
      const int it = level_item_at(menu[lv], x, y);
      if (it == -2) continue;
      if (it < 0) return true;
      const MenuEntry m = menu[lv].items[it];
      if (!m.enabled) return true;
      if (!m.sub.empty()) {
        hover_menu(x, y);
        return true;
      }
      close_menu();
      run(m.id);
      return true;
    }
    close_menu();  // clicked outside: closes, and the click is used up
    return true;
  }

  // ---- the menus ----
  void desktop_menu(double x, double y) {
    std::vector<MenuEntry> view = {entry("Large icons", "size:large", true, cfg.icon_size == DesktopIconSize::Large, true),
                                   entry("Medium icons", "size:medium", true, cfg.icon_size == DesktopIconSize::Medium, true),
                                   entry("Small icons", "size:small", true, cfg.icon_size == DesktopIconSize::Small, true),
                                   sep(),
                                   entry("Auto arrange icons", "toggle:arrange", true, cfg.auto_arrange),
                                   entry("Align icons to grid", "toggle:grid", true, cfg.align_to_grid),
                                   sep(),
                                   entry("Show desktop icons", "toggle:icons", true, cfg.show_icons)};
    std::vector<MenuEntry> sort = {entry("Name", "sort:name", true, cfg.sort_key == SortKey::Name, true), entry("Size", "sort:size", true, cfg.sort_key == SortKey::Size, true),
                                   entry("Item type", "sort:type", true, cfg.sort_key == SortKey::Type, true),
                                   entry("Date modified", "sort:modified", true, cfg.sort_key == SortKey::Modified, true)};
    std::vector<MenuEntry> items;
    items.push_back(submenu("View", std::move(view)));
    items.push_back(submenu("Sort by", std::move(sort)));
    items.push_back(entry("Refresh", "refresh"));
    items.push_back(sep());
    items.push_back(entry("Paste", "paste"));
    items.push_back(sep());
    items.push_back(submenu("New", {entry("Folder", "new:folder"), entry("Text Document", "new:text")}));
    items.push_back(sep());
    items.push_back(entry("Open file manager", "files"));
    items.push_back(entry("Display settings", "settings:display"));
    items.push_back(entry("Personalize", "settings:theme"));
    open_menu_at(std::move(items), x, y);
  }
  void icon_menu(double x, double y) {
    bool special = false;
    int count = 0;
    for (const DesktopIcon& i : icons)
      if (selected.count(i.key)) {
        ++count;
        special = special || i.special;
      }
    MenuEntry open = entry("Open", "open");
    open.bold = true;
    std::vector<MenuEntry> items = {open, entry("Open file location", "location", !special), sep(), entry("Rename", "rename", !special && count == 1), entry("Delete", "delete", !special), sep(),
                                    entry("Properties", "properties", !special && count == 1)};
    open_menu_at(std::move(items), x, y);
  }

  // ---- commands ----
  void run(const std::string& id) {
    if (id.compare(0, 5, "size:") == 0) {
      cfg.icon_size = id == "size:large" ? DesktopIconSize::Large : (id == "size:small" ? DesktopIconSize::Small : DesktopIconSize::Medium);
      save();
      relayout();
    } else if (id == "toggle:arrange") {
      cfg.auto_arrange = !cfg.auto_arrange;
      if (!cfg.auto_arrange) {  // freeze where they are
        for (const DesktopIcon& i : icons) cfg.cells[i.key] = {i.col, i.row};
      }
      save();
      relayout();
    } else if (id == "toggle:grid") {
      cfg.align_to_grid = !cfg.align_to_grid;
      save();
    } else if (id == "toggle:icons") {
      cfg.show_icons = !cfg.show_icons;
      save();
    } else if (id.compare(0, 5, "sort:") == 0) {
      cfg.sort_key = id == "sort:size" ? SortKey::Size : (id == "sort:type" ? SortKey::Type : (id == "sort:modified" ? SortKey::Modified : SortKey::Name));
      cfg.cells.clear();  // sorting re-lays the icons out
      save();
      reload();
    } else if (id == "refresh") {
      reload();
    } else if (id == "paste") {
      paste();
    } else if (id == "new:folder" || id == "new:text") {
      create_new(id == "new:text");
    } else if (id == "files") {
      fm::spawn_detached({"fleetwm-fm", desktop_dir});
    } else if (id == "settings:display") {
      fm::spawn_detached({"fleetwm-settings", "--page", "display"});
    } else if (id == "settings:theme") {
      fm::spawn_detached({"fleetwm-settings", "--page", "theme"});
    } else if (id == "open") {
      for (const DesktopIcon& i : icons)
        if (selected.count(i.key)) open(i);
    } else if (id == "location" || id == "properties") {
      for (const DesktopIcon& i : icons)
        if (selected.count(i.key)) fm::spawn_detached({"fleetwm-fm", desktop_dir});
    } else if (id == "rename") {
      for (const DesktopIcon& i : icons)
        if (selected.count(i.key) && !i.special) begin_rename(i.key);
    } else if (id == "delete") {
      delete_selected();
    }
    redraw();
  }
  void open(const DesktopIcon& i) {
    if (i.special || i.is_dir) fm::spawn_detached({"fleetwm-fm", i.target});
    else fm::open_default(i.target);
  }
  void create_new(bool text) {
    std::string name;
    if (text) {
      name = unique_name(desktop_dir, "New Text Document.txt");
      std::ofstream(desktop_dir + "/" + name).put('\n');
    } else {
      name = new_folder_name(desktop_dir);
      make_dir(desktop_dir + "/" + name);
    }
    select_after_reload = name;
    rename_after_reload = true;
    reload();
  }
  void delete_selected() {
    for (const DesktopIcon& i : icons)
      if (selected.count(i.key) && !i.special) {
        std::string err;
        trash.put(i.target, &err);
      }
    selected.clear();
    reload();
  }
  void paste() {
    app.paste_mime({"x-special/gnome-copied-files", "text/uri-list"}, [this](const std::string& mime, const std::string& data) {
      std::string body = data;
      bool cut = false;
      if (mime == "x-special/gnome-copied-files") {
        const size_t nl = data.find('\n');
        cut = data.compare(0, 3, "cut") == 0;
        body = nl == std::string::npos ? std::string() : data.substr(nl + 1);
      }
      std::vector<std::string> paths = parse_path_list(body);
      if (paths.empty()) return;
      std::thread([this, paths, cut] {
        TransferOptions o;
        o.move = cut;
        o.on_conflict = [](const ConflictInfo&) { return Conflict::KeepBoth; };
        Transfer(paths, desktop_dir, o).run();
        app.post([this] { reload(); });
      }).detach();
    });
  }
  void begin_rename(const std::string& key) {
    for (const DesktopIcon& i : icons)
      if (i.key == key && !i.special) {
        rename_key = key;
        edit.set(i.label);
        edit.select_stem();
      }
  }
  void commit_rename() {
    if (rename_key.empty()) return;
    const std::string old = rename_key, neu = edit.text;
    rename_key.clear();
    if (old == neu) return;
    std::string err;
    if (rename_in_place(desktop_dir + "/" + old, neu, &err)) select_after_reload = neu;
    reload();
  }

  // ---- input ----
  void motion(double x, double y) {
    mx = x;
    my = y;
    if (!menu.empty()) {
      hover_menu(x, y);
      return;
    }
    const int h = cfg.show_icons ? desktop_icon_at(icons, grid, static_cast<int>(x), static_cast<int>(y)) : -1;
    if (down && !band && !drag_key.empty() && !dragging && std::hypot(x - press_x, y - press_y) > 6 && !cfg.auto_arrange) dragging = true;
    if (band) {
      selected.clear();
      for (int k : desktop_icons_in_rect(icons, grid, static_cast<int>(press_x), static_cast<int>(press_y), static_cast<int>(x), static_cast<int>(y))) selected.insert(icons[k].key);
    }
    if (h != hover || dragging || band) {
      hover = h;
      redraw();
    }
  }
  void button(double x, double y, uint32_t b, bool pressed) {
    mx = x;
    my = y;
    constexpr uint32_t kLeft = 0x110, kRight = 0x111;
    if (!menu.empty()) {
      if (pressed) click_menu(x, y);
      return;
    }
    if (!rename_key.empty() && pressed) commit_rename();
    const int i = cfg.show_icons ? desktop_icon_at(icons, grid, static_cast<int>(x), static_cast<int>(y)) : -1;
    if (b == kRight && pressed) {
      if (i >= 0) {
        if (!selected.count(icons[i].key)) selected = {icons[i].key};
        icon_menu(x, y);
      } else {
        selected.clear();
        desktop_menu(x, y);
      }
      redraw();
      return;
    }
    if (b != kLeft) return;
    if (pressed) {
      down = true;
      press_x = x;
      press_y = y;
      drag_key.clear();
      if (i >= 0) {
        const double t = now_s();
        const bool dbl = t - last_click < 0.45 && last_click_icon == i;
        last_click = t;
        last_click_icon = i;
        if (dbl) {
          open(icons[i]);
          return;
        }
        if (!selected.count(icons[i].key)) selected = {icons[i].key};
        drag_key = icons[i].key;
      } else {
        selected.clear();
        band = true;
        last_click_icon = -1;
      }
      redraw();
    } else {
      if (dragging && cfg.align_to_grid) {
        const Pt o = desktop_cell_origin(grid, 0, 0);
        (void)o;
        const auto cell = desktop_cell_at(grid, static_cast<int>(x), static_cast<int>(y));
        if (move_desktop_icon(icons, grid, &cfg, drag_key, cell.first, cell.second)) {
          save();
          place_desktop_icons(&icons, grid, cfg);
        }
      } else if (dragging) {
        const auto cell = desktop_cell_at(grid, static_cast<int>(x), static_cast<int>(y));
        if (move_desktop_icon(icons, grid, &cfg, drag_key, cell.first, cell.second)) {
          save();
          place_desktop_icons(&icons, grid, cfg);
        }
      }
      down = band = dragging = false;
      drag_key.clear();
      redraw();
    }
  }
  void key(const kit::KeyEvent& e) {
    if (!e.pressed) return;
    if (!menu.empty()) {
      if (e.sym == XKB_KEY_Escape) close_menu();
      return;
    }
    if (!rename_key.empty()) {
      const bool shift = e.mods & kit::kShift, ctrl = e.mods & kit::kCtrl;
      switch (e.sym) {
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter: commit_rename(); break;
        case XKB_KEY_Escape: rename_key.clear(); break;
        case XKB_KEY_Left: edit.left(shift, ctrl); break;
        case XKB_KEY_Right: edit.right(shift, ctrl); break;
        case XKB_KEY_Home: edit.home(shift); break;
        case XKB_KEY_End: edit.end(shift); break;
        case XKB_KEY_BackSpace: edit.backspace(ctrl); break;
        case XKB_KEY_Delete: edit.del(ctrl); break;
        default:
          if (ctrl && (e.sym == XKB_KEY_a || e.sym == XKB_KEY_A)) edit.select_all();
          else if (!ctrl && !e.utf8.empty() && static_cast<unsigned char>(e.utf8[0]) >= 0x20) edit.insert(e.utf8);
      }
      redraw();
      return;
    }
    if (e.sym == XKB_KEY_F2) run("rename");
    else if (e.sym == XKB_KEY_Delete) run("delete");
    else if (e.sym == XKB_KEY_F5) reload();
    else if (e.sym == XKB_KEY_Return) run("open");
    else if ((e.mods & kit::kCtrl) && (e.sym == XKB_KEY_a || e.sym == XKB_KEY_A)) {
      for (const DesktopIcon& i : icons) selected.insert(i.key);
    }
    redraw();
  }
};

}  // namespace

int main(int argc, char** argv) {
  block_quit_signals();
  if (handle_info_flags(argc, argv, "fleetwm-desktop", "[--output NAME] | --screenshot FILE [--size WxH] [--menu desktop|icon|view] [--hint]")) return 0;
  tune_malloc_for_low_rss();
  Desktop D;
  // Offline picture of the desktop (or of the shortcut card) on a plain background, for checking the look without a compositor.
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) != "--screenshot" || i + 1 >= argc) continue;
    int w = 1024, h = 768;
    std::string menu, out = argv[i + 1];
    bool hint = false;
    for (int k = 1; k < argc; ++k) {
      if (std::string(argv[k]) == "--size" && k + 1 < argc) std::sscanf(argv[k + 1], "%dx%d", &w, &h);
      if (std::string(argv[k]) == "--menu" && k + 1 < argc) menu = argv[k + 1];
      if (std::string(argv[k]) == "--hint") hint = true;
    }
    D.reload();
    D.width = w;
    D.height = h;
    D.relayout();
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(surf);
    cairo_set_source_rgb(cr, 0.16, 0.30, 0.52);  // stands in for the wallpaper
    cairo_paint(cr);
    if (hint) {
      D.caps = split_key_combo(format_key_combo(load_keybinds_config().shortcuts_help));
      cairo_translate(cr, w - 260, h - 56);
      D.draw_hint(cr, 244, 40);
    } else {
      if (!D.icons.empty()) D.selected.insert(D.icons[std::min<size_t>(3, D.icons.size() - 1)].key);
      if (menu == "desktop") D.desktop_menu(560, 300);
      if (menu == "view") {
        D.desktop_menu(560, 300);
        D.mx = 600;
        D.my = 322;
        D.hover_menu(600, 322);
      }
      if (menu == "icon") D.icon_menu(200, 200);
      D.draw(cr, w, h);
    }
    cairo_destroy(cr);
    const bool ok = cairo_surface_write_to_png(surf, out.c_str()) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(surf);
    return ok ? 0 : 1;
  }
  for (int i = 1; i + 1 < argc; ++i)
    if (std::string(argv[i]) == "--output") D.app.set_preferred_output(argv[i + 1]);
  if (!D.app.connect()) return 1;
  D.reload();
  D.apply_mode();
  const char* h = std::getenv("HOME");
  const std::string home = h && *h ? h : "/";
  // the Desktop folder, the config folder (desktop.toml, theme.toml: the layout) and the themes
  kit::watch_dirs(D.app, {D.desktop_dir, std::filesystem::path(user_config_path()).parent_path().string()}, [&D] {
    D.reload();
    D.apply_mode();
  });
  (void)home;
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  const int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  D.app.watch_fd(sfd, [&D] { D.app.quit(); });
  D.app.run();
  return 0;
}
