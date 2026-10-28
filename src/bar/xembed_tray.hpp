#pragma once

// XEmbed system tray (the old _NET_SYSTEM_TRAY protocol) for programs that have no StatusNotifierItem:
// Wine and Proton programs (the Rockstar launcher, Steam's old tray, Battle.net) put their icon there.
// The bar owns the tray selection on the compositor's Xwayland, parks each docked icon window out of
// sight, redirects it with the Composite extension and copies its pixels, so the bar draws the icon itself.
// Clicks are sent to the icon window as X button events.
//
// Xwayland starts on demand, so the bar never connects first (that would keep it running for nothing): it
// waits until the Xwayland that serves $DISPLAY is running, then connects.

#include <cairo.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fleetkit.hpp"

namespace fleetwm::bar {

class XembedTray {
 public:
  struct Icon {
    cairo_surface_t* surface = nullptr;  // premultiplied ARGB32, icon_px square; owned by the tray
    std::string title;
  };

  XembedTray(kit::App& app, std::function<void()> on_change);
  ~XembedTray();
  XembedTray(const XembedTray&) = delete;
  XembedTray& operator=(const XembedTray&) = delete;

  void start();
  void set_icon_px(int px);
  const std::vector<Icon>& icons() const;
  // button: Linux evdev code (BTN_LEFT=0x110, BTN_RIGHT=0x111, BTN_MIDDLE=0x112)
  void click(size_t index, uint32_t button, int x, int y);

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

// Pure parts, for the tests.
// True when `comm` (a /proc/<pid>/comm line) names an Xwayland server.
bool is_xwayland_comm(const std::string& comm);
// Converts a 32-bit BGRX (little-endian) X image to premultiplied ARGB32 with alpha 255, `stride` bytes per row.
void bgrx_to_argb(const unsigned char* src, int src_stride, unsigned char* dst, int dst_stride, int w, int h);
// Converts a 32-bit BGRA (little-endian, premultiplied) X image to cairo's ARGB32, clamping colour values above alpha.
void bgra_to_argb(const unsigned char* src, int src_stride, unsigned char* dst, int dst_stride, int w, int h);
// X button number for an evdev button code (0 for buttons the tray does not forward).
int x_button_for(uint32_t evdev);

}  // namespace fleetwm::bar
