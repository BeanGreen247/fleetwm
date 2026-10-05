#pragma once

// StatusNotifierItem host + watcher (the freedesktop systray protocol) on
// the session bus via sd-bus, GTK/GLib-free. The bar draws the icons itself.

#include <cairo.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lean.hpp"

namespace fleetwm::bar {

class Tray {
 public:
  struct Item {
    std::string bus_name;
    std::string object_path;
    cairo_surface_t* icon = nullptr;  // premultiplied ARGB32, may be null until loaded
    std::string title;
  };

  Tray(lean::App& app, std::function<void()> on_change);
  ~Tray();
  Tray(const Tray&) = delete;
  Tray& operator=(const Tray&) = delete;

  void start();
  const std::vector<Item>& items() const;
  // button: Linux evdev code (BTN_LEFT=0x110, BTN_RIGHT=0x111, BTN_MIDDLE=0x112)
  void click(size_t index, uint32_t button, int x, int y);

 public:
  struct Impl;  // public only so the extern "C" sd-bus callbacks can name it

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace fleetwm::bar
