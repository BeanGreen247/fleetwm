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
  if (ls->configured) {
    return;
  }
  // wlroots rejects wlr_layer_surface_v1_configure() until the surface's
  // first commit has happened (it establishes pending.layer/anchors/size
  // internally) -- this is that first commit, so it's now safe to send
  // the initial configure. Configure with the output's full effective
  // box; no exclusive-zone accumulation across sibling layer surfaces yet
  // (see docs/adr/0008) -- an unanchored surface like the launcher is
  // centered automatically by wlr_scene_layer_surface_v1's own commit
  // handling, so the full box is already correct for that case.
  wlr_box box{};
  wlr_output_layout_get_box(ls->server->output_layout(), ls->layer_surface->output, &box);
  wlr_layer_surface_v1_configure(ls->layer_surface, static_cast<uint32_t>(box.width),
                                  static_cast<uint32_t>(box.height));
  ls->configured = true;
}

}  // namespace fleetwm
