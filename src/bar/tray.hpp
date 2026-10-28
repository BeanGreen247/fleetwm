#pragma once

// StatusNotifierItem host + watcher (the freedesktop systray protocol) on
// the session bus via sd-bus, GTK/GLib-free. The bar draws the icons itself.

#include <cairo.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fleetkit.hpp"

namespace fleetwm::bar {

class Tray {
 public:
  struct Item {
    std::string bus_name;
    std::string object_path;
    cairo_surface_t* icon = nullptr;  // premultiplied ARGB32, may be null until loaded
    std::string title;
    bool xembed = false;  // an X11 (Wine) tray icon, not a StatusNotifierItem
  };

  // A StatusNotifierItem that has no Activate / ContextMenu method but a `Menu` (com.canonical.dbusmenu: Steam, anything built on
  // libappindicator) is clicked by showing that menu: the tray reads its layout and hands the entries to this callback.
  struct MenuEntry {
    int id = 0;            // dbusmenu item id (what Event "clicked" takes)
    std::string label;
    bool separator = false;
    int children = 0;       // >0: a submenu header followed by that many entries (its own and its submenus')
    bool checked = false;   // a ticked check or radio entry
    bool enabled = true;    // shown greyed when not
  };
  struct MenuRequest {
    std::string bus_name, menu_path;
    std::vector<MenuEntry> entries;
    int x = 0, y = 0;  // where the click was, in bar coordinates
  };
  void set_menu_handler(std::function<void(const MenuRequest&)> handler);

  Tray(kit::App& app, std::function<void()> on_change);
  ~Tray();
  Tray(const Tray&) = delete;
  Tray& operator=(const Tray&) = delete;

  void start();
  void set_icon_px(int px);  // size the X11 tray icons are captured at
  const std::vector<Item>& items() const;
  // button: Linux evdev code (BTN_LEFT=0x110, BTN_RIGHT=0x111, BTN_MIDDLE=0x112)
  void click(size_t index, uint32_t button, int x, int y);
  // Asks the item for its hover text (the ToolTip property: title, then description) and
  // calls `done` with it; "" when the item has none. The text is fetched live, so items can
  // build it at the moment you hover.
  void tooltip(size_t index, std::function<void(std::string)> done);

 public:
  struct Impl;  // public only so the extern "C" sd-bus callbacks can name it

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace fleetwm::bar
