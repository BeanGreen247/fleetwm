#include "layer_surface.hpp"

extern "C" {
#include <wlr/types/wlr_xdg_shell.h>
}

#include "server.hpp"

namespace fleetwm {

LayerSurface::LayerSurface(Server* server_, wlr_layer_surface_v1* layer_surface_)
    : server(server_), layer_surface(layer_surface_) {}

LayerSurface::~LayerSurface() = default;

void layer_surface_map(wl_listener* listener, void*) {
  LayerSurface* ls = wl_container_of(listener, ls, map);
  if (ls->layer_surface->current.keyboard_interactive !=
      ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
    ls->server->focus_layer_surface(ls);
  }
}

void layer_surface_unmap(wl_listener* listener, void*) {
  LayerSurface* ls = wl_container_of(listener, ls, unmap);
  wlr_seat* seat = ls->server->seat();
  if (seat->keyboard_state.focused_surface == ls->surface()) {
    ls->server->focus_view(nullptr);
  }
}

void layer_surface_destroy(wl_listener* listener, void*) {
  LayerSurface* ls = wl_container_of(listener, ls, destroy);
  Server* server = ls->server;
  server->layer_surfaces.remove_if(
      [ls](const std::unique_ptr<LayerSurface>& l) { return l.get() == ls; });
}

void layer_surface_new_popup(wl_listener* listener, void* data) {
  LayerSurface* ls = wl_container_of(listener, ls, new_popup);
  auto* popup = static_cast<wlr_xdg_popup*>(data);
  // Parent the popup's scene node into this layer surface's own tree so
  // it stacks correctly above the layer surface's main content. Full
  // popup lifecycle (output-box unconstraining, grabs) is out of scope
  // for launcher MVP, which creates no popups -- this is enough for the
  // popup to at least render if one ever appears.
  wlr_scene_xdg_surface_create(ls->scene_layer_surface->tree, popup->base);
}

void layer_surface_surface_commit(wl_listener* listener, void*) {
  LayerSurface* ls = wl_container_of(listener, ls, surface_commit);
  // wlroots only flips wlr_layer_surface_v1::initialized to true inside
  // its own surface-commit handling (types/wlr_layer_shell_v1.c,
  // layer_surface_role_commit -- read directly from wlroots 0.18.2
  // source on fleetwm-dev). Any configure before that point
  // unconditionally fails with "A configure is sent to an uninitialized
  // wlr_layer_surface_v1", regardless of what wlr_layer_shell_v1.h's
  // doc comment implies about configuring during new_surface.
  // initial_commit is wlroots' own flag for exactly this moment.
  if (!ls->layer_surface->initial_commit) {
    return;
  }

  // wlr_scene_layer_surface_v1_configure() (not the raw
  // wlr_layer_surface_v1_configure) so the scene helper also positions
  // the node per the client's now-populated anchors/margins in
  // layer_surface->current. No exclusive-zone accumulation across
  // sibling layer surfaces yet (see docs/adr/0008), so full_area ==
  // usable_area.
  wlr_box full_area{};
  wlr_output_layout_get_box(ls->server->output_layout(), ls->layer_surface->output, &full_area);
  wlr_box usable_area = full_area;
  wlr_scene_layer_surface_v1_configure(ls->scene_layer_surface, &full_area, &usable_area);
}

}  // namespace fleetwm
