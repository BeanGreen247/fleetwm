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
class Output;

// A toplevel window, native (xdg_toplevel) or XWayland. Phase 1's
// master-stack algorithm lives in Output::relayout(), driven externally;
// View just exposes the pinned/floating flags relayout() reads to decide
// whether to skip a view, plus the border-rect nodes it (and focus
// tracking) render into.
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
  // Set at map time alongside workspace; unmap needs it to trigger a
  // relayout of the remaining tiled views after workspace is cleared,
  // and relayout() itself needs it for output-box geometry.
  Output* output = nullptr;

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

  // Opts this view out of Output::relayout()'s master-stack math,
  // independent of pinned -- floating (unlike pinned) keeps the view in
  // its normal workspace/layer_toplevels_ position, just skipped by
  // tiling so it keeps whatever position/size it last had.
  bool floating = false;

  // Whether this view currently holds keyboard focus -- Server::focus_view
  // sets this on the old/new focused view on every focus change and calls
  // resize_border() so the focus indicator stays in sync without every
  // caller needing to know about borders.
  bool focused = false;

  // Sets/clears the border color and thickness used to indicate pinned
  // state. Safe to call before the view has mapped (border rects exist
  // from construction; resize_border() below applies real dimensions
  // once the surface's actual size is known at map time).
  void set_pinned(bool pinned);
  void set_floating(bool floating);
  void resize_border();

  // Current border thickness in px, per the same pinned/focused priority
  // resize_border() uses internally -- relayout() needs this to size the
  // toplevel's content area (container_tree's box minus border) rather
  // than the outer box, since wlr_xdg_toplevel_set_size sets the client's
  // surface size, not container_tree's.
  int border_thickness() const;

  wl_listener map{};
  wl_listener unmap{};
  wl_listener destroy{};
  wl_listener request_move{};
  wl_listener request_resize{};
  wl_listener surface_commit{};
  wl_listener new_popup{};

#if FLEETWM_XWAYLAND
  wlr_xwayland_surface* xwayland_surface = nullptr;
  wl_listener request_configure{};
#endif

  wlr_surface* surface() const;
  void focus();
  void close();
};

}  // namespace fleetwm
