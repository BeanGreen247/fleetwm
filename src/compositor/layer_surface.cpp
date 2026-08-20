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

namespace {

// Same PopupHandle shape as xdg_toplevel_new_popup's copy in server.cpp
// (kept as a separate local copy rather than shared, matching this
// codebase's existing per-file small-helper style) -- without this, a
// layer surface's own popups (e.g. fleetwm-bar's power menu, a
// GtkMenuButton popover) never receive their initial configure and just
// hang forever: the button itself still responds to the click (internal
// GTK state toggles), but the popup surface never finishes initializing
// so nothing ever renders, which reads as "the power menu does nothing".
struct LayerPopupHandle {
  Server* server;
  wlr_xdg_popup* popup;
  wl_listener commit{};
  wl_listener destroy{};
};

void layer_popup_handle_destroy(wl_listener* listener, void*) {
  LayerPopupHandle* handle = wl_container_of(listener, handle, destroy);
  wl_list_remove(&handle->commit.link);
  wl_list_remove(&handle->destroy.link);
  delete handle;
}

void layer_popup_handle_commit(wl_listener* listener, void*) {
  LayerPopupHandle* handle = wl_container_of(listener, handle, commit);
  if (!handle->popup->base->initial_commit) {
    return;
  }
  wlr_box output_box{};
  wlr_output_layout_get_box(handle->server->output_layout(), nullptr, &output_box);
  wlr_xdg_popup_unconstrain_from_box(handle->popup, &output_box);
}

}  // namespace

void layer_surface_new_popup(wl_listener* listener, void* data) {
  LayerSurface* ls = wl_container_of(listener, ls, new_popup);
  auto* popup = static_cast<wlr_xdg_popup*>(data);
  // Parent the popup's scene node into this layer surface's own tree so
  // it stacks correctly above the layer surface's main content.
  wlr_scene_xdg_surface_create(ls->scene_layer_surface->tree, popup->base);

  auto* handle = new LayerPopupHandle{ls->server, popup};
  handle->commit.notify = layer_popup_handle_commit;
  wl_signal_add(&popup->base->surface->events.commit, &handle->commit);
  handle->destroy.notify = layer_popup_handle_destroy;
  wl_signal_add(&popup->base->events.destroy, &handle->destroy);
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
  // initial_commit is wlroots' own flag for exactly this moment, so it
  // still gates whether a configure is even legal to send here.
  //
  // wlr_scene_layer_surface_v1_configure() (not the raw
  // wlr_layer_surface_v1_configure) so the scene helper also
  // (re-)positions the node per the client's current anchors/margins/
  // size. This used to run only on the very first commit -- correct for
  // a surface whose size the client never changes after mapping (e.g.
  // the full-width bar, always anchored to both side edges so its size
  // is fixed by the output width regardless), but wrong for anything
  // that legitimately resizes itself later: a client-driven natural-
  // width surface (fleetwm-bar's Island layout, bar_config.hpp) whose
  // content changes after the window is already shown -- most commonly
  // its systray gaining icons a moment after mapping, once
  // StatusNotifierItem registrations start arriving over D-Bus -- sent
  // a new, larger size on a later commit that the scene graph never
  // picked up, since nothing re-called the scene helper for it. The
  // surface's own wl_surface buffer still rendered at its real (new,
  // larger) size, but the scene node's cached position/bounds from the
  // first commit never moved to match, producing a visibly
  // mispositioned/overflowing surface -- reproduced live while building
  // Island mode. Re-configuring on every commit (not just the first)
  // keeps position/size in sync with whatever the client's current
  // committed state actually is, matching wlroots' own tinywl.c
  // reference, which does not gate this call on initial_commit either.
  // Passing full_area as usable_area (rather than an already-shrunk
  // box) is unchanged from before -- a surface's own exclusive zone
  // should not shrink the area it is itself positioned against; other
  // surfaces' zones are accounted for separately via
  // Output::update_usable_area() below.
  if (ls->layer_surface->initialized) {
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
