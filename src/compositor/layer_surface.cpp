#include "layer_surface.hpp"

extern "C" {
#include <wlr/types/wlr_xdg_shell.h>
}

#include "output.hpp"
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
  // Drop this surface's exclusive-zone reservation, if any -- surface()
  // ->mapped is already false by this point (wlroots clears it before
  // firing unmap), so update_usable_area()'s scan naturally skips it.
  Output* output = ls->server->output_for(ls->layer_surface->output);
  if (output) {
    output->update_usable_area();
  }
}

void layer_surface_destroy(wl_listener* listener, void*) {
  LayerSurface* ls = wl_container_of(listener, ls, destroy);
  Server* server = ls->server;
  // Remove listeners before erasing ls -- see xdg_toplevel_destroy
  // (server.cpp) for the full explanation of why this ordering matters
  // (tinywl.c reference pattern; a destructor-based removal invoked
  // indirectly through remove_if crashed for View under the equivalent
  // real-world teardown race, so this is fixed the same way up front
  // rather than waiting to hit the same bug here too).
  wl_list_remove(&ls->map.link);
  wl_list_remove(&ls->unmap.link);
  wl_list_remove(&ls->destroy.link);
  wl_list_remove(&ls->new_popup.link);
  wl_list_remove(&ls->surface_commit.link);
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
  bool first_configure = ls->layer_surface->initial_commit;
  if (first_configure) {
    // wlr_scene_layer_surface_v1_configure() (not the raw
    // wlr_layer_surface_v1_configure) so the scene helper also positions
    // the node per the client's now-populated anchors/margins in
    // layer_surface->current. Passing full_area as usable_area here
    // (rather than an already-shrunk box) matches wlroots' own
    // tinywl.c reference for the very first configure -- this
    // surface's own exclusive zone should not shrink the area it is
    // itself positioned against. Other surfaces' zones are accounted
    // for separately via Output::update_usable_area() below.
    wlr_box full_area{};
    wlr_output_layout_get_box(ls->server->output_layout(), ls->layer_surface->output, &full_area);
    wlr_box usable_area = full_area;
    wlr_scene_layer_surface_v1_configure(ls->scene_layer_surface, &full_area, &usable_area);
  }

  // Recompute exclusive-zone accumulation whenever this surface's
  // committed state could have changed it (its exclusive_zone/anchor, or
  // simply having just become mapped for the first time). update_usable_area()
  // walks every mapped layer surface on the output from scratch (see
  // output.cpp), so it's safe -- if a bit redundant -- to call on every
  // commit rather than only when exclusive_zone actually changed.
  Output* output = ls->server->output_for(ls->layer_surface->output);
  if (output) {
    output->update_usable_area();
  }
}

}  // namespace fleetwm
