#pragma once

#include <wayland-server-core.h>

extern "C" {
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
}

#include "config.h"
#include "scene_node_owner.hpp"

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

  // Must stay first: hit-testing (server.cpp) reads a scene node's
  // node.data as SceneNodeOwner* before reinterpreting further.
  SceneNodeOwner scene_node_owner = SceneNodeOwner::View;

  Server* server;
  Kind kind;
  Workspace* workspace = nullptr;

  // container_tree_ is the outer wrapper: it holds the four border rects
  // plus scene_tree (the actual surface content) as a child, offset by
  // border_thickness so the border frames the content rather than
  // overlapping it. Positioning/reparenting-for-pin acts on
  // container_tree_; hit-testing and focus-raise still act on scene_tree,
  // since that's what carries the SceneNodeOwner tag (see
  // scene_node_at() in server.cpp, which walks up past untagged
  // ancestors like container_tree_ to find it).
  wlr_scene_tree* container_tree = nullptr;
  wlr_scene_tree* scene_tree = nullptr;
  wlr_scene_rect* border_top = nullptr;
  wlr_scene_rect* border_bottom = nullptr;
  wlr_scene_rect* border_left = nullptr;
  wlr_scene_rect* border_right = nullptr;
  wlr_xdg_toplevel* xdg_toplevel = nullptr;

  // Whether this view is pinned always-on-top (PowerToys-style): its
  // container_tree lives in Server's layer_pinned_ tree instead of
  // layer_toplevels_ while pinned, so it stays visible and on top across
  // every workspace switch, not just within its own workspace -- see
  // Output::switch_workspace(), which never touches layer_pinned_.
  bool pinned = false;

  // Sets/clears the border color and thickness used to indicate pinned
  // state. Safe to call before the view has mapped (border rects exist
  // from construction; resize_border() below applies real dimensions
  // once the surface's actual size is known at map time).
  void set_pinned(bool pinned);
  void resize_border();

  wl_listener map{};
  wl_listener unmap{};
  wl_listener destroy{};
  wl_listener request_move{};
  wl_listener request_resize{};
  wl_listener surface_commit{};

#if FLEETWM_XWAYLAND
  wlr_xwayland_surface* xwayland_surface = nullptr;
  wl_listener request_configure{};
#endif

  wlr_surface* surface() const;
  void focus();
  void close();
};

}  // namespace fleetwm
