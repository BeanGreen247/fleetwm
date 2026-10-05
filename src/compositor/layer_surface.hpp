#pragma once

#include <wayland-server-core.h>

extern "C" {
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>
}

#include "scene_node_owner.hpp"

namespace fleetwm {

class Server;

// A wlr-layer-shell-v1 surface (background/bottom/top/overlay), e.g. a
// gtk4-layer-shell popup like fleetwm-launcher, or (future) fleetwm-bar.
// Unlike View, a LayerSurface does not belong to any Workspace -- its
// scene node lives in one of Server's always-enabled layer trees and is
// never touched by Output::switch_workspace().
class LayerSurface {
 public:
  LayerSurface(Server* server, wlr_layer_surface_v1* layer_surface);
  ~LayerSurface();

  // Must stay first: hit-testing (server.cpp) reads a scene node's
  // node.data as SceneNodeOwner* before reinterpreting further.
  SceneNodeOwner scene_node_owner = SceneNodeOwner::LayerSurface;

  Server* server;
  wlr_layer_surface_v1* layer_surface;

  // Owned by wlroots (freed with the underlying wlr_layer_surface_v1);
  // wraps the wlr_scene_tree parented into one of Server's layer_* trees
  // plus popup/subsurface handling.
  wlr_scene_layer_surface_v1* scene_layer_surface = nullptr;

  wl_listener map{};
  wl_listener unmap{};
  wl_listener destroy{};
  wl_listener new_popup{};
  wl_listener surface_commit{};

  wlr_surface* surface() const { return layer_surface->surface; }
};

// Registered on LayerSurface::map/unmap/destroy/new_popup/surface_commit
// by server_new_layer_surface() (server.cpp). Defined in layer_surface.cpp.
void layer_surface_map(wl_listener* listener, void* data);
void layer_surface_unmap(wl_listener* listener, void* data);
void layer_surface_destroy(wl_listener* listener, void* data);
void layer_surface_new_popup(wl_listener* listener, void* data);
void layer_surface_surface_commit(wl_listener* listener, void* data);

}  // namespace fleetwm
