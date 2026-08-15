#pragma once

#include <wayland-server-core.h>

extern "C" {
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
}

#include "config.h"

#if FLEETWM_XWAYLAND
extern "C" {
#include <wlr/xwayland.h>
}
#endif

namespace fleetwm {

class Server;
class Workspace;

// A toplevel window, native (xdg_toplevel) or XWayland. Phase 0 does no
// real tiling: a View is placed at a fixed origin sized to its output at
// map time and left alone (see server.cpp new_xdg_toplevel handling).
// Phase 1 replaces that placement call with the master-stack algorithm;
// View itself doesn't need to change for that since layout is driven
// externally by Output::relayout(), not by View.
class View {
 public:
  enum class Kind { XdgToplevel, XWayland };

  View(Server* server, Kind kind);
  ~View();

  Server* server;
  Kind kind;
  Workspace* workspace = nullptr;

  wlr_scene_tree* scene_tree = nullptr;
  wlr_xdg_toplevel* xdg_toplevel = nullptr;

  wl_listener map{};
  wl_listener unmap{};
  wl_listener destroy{};
  wl_listener request_move{};
  wl_listener request_resize{};

#if FLEETWM_XWAYLAND
  wlr_xwayland_surface* xwayland_surface = nullptr;
  wl_listener request_configure{};
#endif

  wlr_surface* surface() const;
  void focus();
  void close();
};

}  // namespace fleetwm
