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
  };

  Tray(kit::App& app, std::function<void()> on_change);
  ~Tray();
  Tray(const Tray&) = delete;
  Tray& operator=(const Tray&) = delete;

  void start();
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
