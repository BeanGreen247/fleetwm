#include "server.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/inotify.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-server-core.h>

extern "C" {
#include <wlr/util/log.h>
}

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>

#include "input.hpp"
#include "ipc_server.hpp"
#include "layer_surface.hpp"
#include "output.hpp"
#include "paths_config.h"
#include "scene_node_owner.hpp"
#include "view.hpp"

namespace fleetwm {

// These trampolines are declared as friends of Server (see server.hpp) so
// they must live directly in the fleetwm namespace, not a nested anonymous
// namespace -- friend declarations name fleetwm::server_new_output etc.
// exactly, and an anonymous-namespace definition is a distinct entity that
// friendship would not reach.

// -- output ------------------------------------------------------------

void server_new_output(wl_listener* listener, void* data) {
  Server* server = wl_container_of(listener, server, new_output_);
  auto* wlr_out = static_cast<wlr_output*>(data);

  wlr_output_init_render(wlr_out, server->allocator_, server->renderer_);

  wlr_output_state state;
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state, true);
  wlr_output_mode* mode = wlr_output_preferred_mode(wlr_out);
  if (mode) {
    wlr_output_state_set_mode(&state, mode);
  }
  wlr_output_commit_state(wlr_out, &state);
  wlr_output_state_finish(&state);

  auto output = std::make_unique<Output>(server, wlr_out);
  wlr_output_layout_add_auto(server->output_layout_, wlr_out);
  wlr_scene_output* scene_output = wlr_scene_output_create(server->scene_, wlr_out);
  wlr_scene_output_layout_add_output(server->scene_layout_,
                                      wlr_output_layout_get(server->output_layout_, wlr_out),
                                      scene_output);
  output->scene_output = scene_output;
  // No layer surfaces exist yet for a brand-new output, so this just seeds
  // usable_area to the full output box (equivalent to the old
  // wlr_output_layout_get_box() call relayout() used directly before
  // exclusive-zone support existed).
  output->update_usable_area();

  server->outputs.push_back(std::move(output));
}

// -- xdg toplevels -------------------------------------------------------

// Re-asserts View::always_on_top within `workspace` -- called after every
// focus change (Server::focus_view) so raising the newly-focused view
// there can never leave it above an always-on-top view (e.g.
// fleetwm-settings) that happens to share the same workspace.
void raise_always_on_top_views(Workspace* workspace) {
  if (!workspace) {
    return;
  }
  for (View* v : workspace->views()) {
    if (v->always_on_top) {
      wlr_scene_node_raise_to_top(&v->container_tree->node);
    }
  }
}

static void xdg_toplevel_map(wl_listener* listener, void*) {
  View* view = wl_container_of(listener, view, map);

  // Border rects are sized off the surface's real geometry, only known
  // once the client has actually mapped (it picks its own size -- see
  // xdg_toplevel_surface_commit's size-0,0 "client decides" configure).
  view->resize_border();

  // fleetwm-settings always opens floating and centered, on top of
  // whatever's tiled in the workspace it lands in -- explicit user
  // request. It's a single-instance panel, not something that should
  // compete for tiling space the way a terminal would. Matched by its
  // GApplication ID (settings/main.cpp's gtk_application_new() call),
  // not window title, since that's stable regardless of locale/theme.
  bool is_settings = view->kind == View::Kind::XdgToplevel && view->xdg_toplevel &&
                      view->xdg_toplevel->app_id &&
                      std::strcmp(view->xdg_toplevel->app_id, "dev.fleetwm.Settings") == 0;
  if (is_settings) {
    view->set_floating(true);
    view->always_on_top = true;
  }

  if (!view->server->outputs.empty()) {
    Output* output = view->server->outputs.front().get();
    Workspace& workspace = output->active_workspace();
    workspace.add_view(view);
    view->workspace = &workspace;
    view->output = output;

    wlr_box box{};
    wlr_output_layout_get_box(view->server->output_layout(), output->wlr_output_ptr, &box);

    if (is_settings) {
      // Center using the toplevel's own committed geometry (known now --
      // see the resize_border() comment above for why).
      wlr_box geo{};
      wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);
      wlr_scene_node_set_position(&view->container_tree->node, box.x + (box.width - geo.width) / 2,
                                   box.y + (box.height - geo.height) / 2);
      wlr_scene_node_raise_to_top(&view->container_tree->node);
    } else {
      // relayout() below handles tiled placement; this is just a sane
      // fallback position (full output box) for the pinned/floating
      // case, which relayout() skips entirely.
      wlr_scene_node_set_position(&view->container_tree->node, box.x, box.y);
    }

    if (view->fullscreen) {
      // A fullscreen request that arrived before this view ever mapped
      // (output was still null -- see View::set_fullscreen's doc
      // comment) never got applied. output is set above now, so
      // re-invoke it -- toggle fullscreen_ off first so set_fullscreen's
      // own no-op-if-unchanged guard doesn't swallow this call. Done
      // before relayout() below so relayout() already sees
      // fullscreen==true and skips tiling this view instead of tiling
      // it for one frame and then correcting.
      view->fullscreen = false;
      view->set_fullscreen(true);
    }
    output->relayout();
  }

  view->server->focus_view(view);
}

static void xdg_toplevel_unmap(wl_listener* listener, void*) {
  View* view = wl_container_of(listener, view, unmap);
  Server* server = view->server;
  bool was_focused = server->seat()->keyboard_state.focused_surface == view->surface();
  Output* output = view->output;

  if (view->workspace) {
    view->workspace->remove_view(view);
    view->workspace = nullptr;
  }
  view->output = nullptr;

  if (output) {
    output->relayout();
  }

  if (!was_focused) {
    return;
  }

  // i3/dwm-style focus-on-close: hand focus to the next visible view in
  // stacking order (server->views is already front-to-back, topmost
  // first -- see focus_view()'s splice-to-front). "Visible" means
  // pinned (always shown) or still enabled on its own workspace's
  // current output -- container_tree->node.enabled already encodes
  // that (see Output::switch_workspace).
  for (const std::unique_ptr<View>& candidate : server->views) {
    if (candidate.get() != view &&
        (candidate->pinned || candidate->container_tree->node.enabled)) {
      server->focus_view(candidate.get());
      return;
    }
  }
  server->focus_view(nullptr);
}

static void xdg_toplevel_destroy(wl_listener* listener, void*) {
  View* view = wl_container_of(listener, view, destroy);
  Server* server = view->server;

  // Remove every one of View's listeners FIRST, before anything else --
  // matches wlroots' own tinywl.c reference pattern exactly (see its
  // xdg_toplevel_destroy). Order matters: the wlr_scene_node_destroy()
  // call below can cascade into wlroots' own internal xdg-surface scene
  // cleanup (scene_xdg_surface_handle_*, see wlroots 0.18.2
  // types/scene/xdg_shell.c), which itself may tear down the underlying
  // wlr_surface's listener lists as part of the same teardown. Removing
  // View's own listeners (map/unmap/surface_commit are registered on
  // that same surface) AFTER letting that cascade run left their
  // wl_list links already invalidated by the time this code tried to
  // wl_list_remove() them -- wl_list_remove() unconditionally
  // dereferences elm->prev/elm->next with no "already removed" guard,
  // so removing an already-invalidated link segfaults (confirmed via
  // gdb: crash was inside wl_list_remove() itself, reproducibly
  // triggered by Alt+Shift+Q closing a window with another still open).
  // Removing everything up front, before any scene/surface teardown
  // cascade has a chance to touch these same links, is what tinywl does
  // and is what keeps this safe.
  wl_list_remove(&view->map.link);
  wl_list_remove(&view->unmap.link);
  wl_list_remove(&view->destroy.link);
  wl_list_remove(&view->request_move.link);
  wl_list_remove(&view->request_resize.link);
  wl_list_remove(&view->request_fullscreen.link);
  wl_list_remove(&view->surface_commit.link);
  wl_list_remove(&view->new_popup.link);
  // NOT removing view->request_configure here even though it's compiled
  // in under FLEETWM_XWAYLAND: nothing in this file (or anywhere else)
  // currently calls wl_signal_add() on it -- XWayland toplevel creation
  // itself isn't implemented yet (see the new_xwayland_surface comment
  // elsewhere in this file). A wl_listener{}'s default-constructed link
  // is unlinked (prev/next both null), and wl_list_remove() unconditionally
  // dereferences elm->prev/elm->next with no null guard -- removing a
  // never-added listener segfaults on those nulls. This was the actual
  // cause of a real crash (Alt+Shift+Q closing a foot window with
  // another still open): confirmed via a temporary per-line DEBUGTRACE
  // fprintf (added, used, removed) showing every *other* listener
  // removed cleanly and the crash landing exactly here. Add the
  // wl_list_remove() back only once something actually wires this
  // listener up via wl_signal_add() for real XWayland toplevels.

  // container_tree is a plain wlr_scene_tree_create(), unlike scene_tree
  // (owned/auto-destroyed by wlr_scene_xdg_surface_create alongside the
  // xdg_surface) -- nothing else destroys it, so it would otherwise leak
  // an empty tree (plus its border-rect children) on every toplevel
  // close. Safe to call even if scene_tree already self-destroyed first.
  wlr_scene_node_destroy(&view->container_tree->node);
  server->views.remove_if(
      [view](const std::unique_ptr<View>& v) { return v.get() == view; });
}

static void xdg_toplevel_surface_commit(wl_listener* listener, void*) {
  View* view = wl_container_of(listener, view, surface_commit);
  // wlroots never auto-sends a toplevel's first configure -- every
  // wlr_xdg_toplevel_set_*() setter schedules one as a side effect, but
  // nothing calls any of them for a brand new toplevel unless the
  // compositor does (read directly from wlroots 0.18.2 source on
  // fleetwm-dev: wlr_xdg_toplevel.c has no such call; only setters that
  // forward to wlr_xdg_surface_schedule_configure()). Spec-compliant
  // clients like foot wait for that first configure before attaching a
  // buffer, so without this they create a surface and then hang forever
  // -- confirmed via real testing: xdg_toplevel_map never fired, no
  // errors logged anywhere, foot just sat there. initial_commit is
  // wlroots' flag for "the surface just initialized, safe to configure
  // now" (same gate as the layer-shell path, see layer_surface.cpp).
  // Size (0, 0) tells the client to pick its own natural size.
  if (view->xdg_toplevel->base->initial_commit) {
    wlr_xdg_toplevel_set_size(view->xdg_toplevel, 0, 0);
    return;
  }
  // Border rects must be sized off the client's just-committed geometry,
  // not the size we last requested -- wlr_xdg_toplevel_set_size() (called
  // from Output::relayout() on every tile/promote/float-toggle) is async:
  // the client hasn't actually resized when that call returns, so drawing
  // borders synchronously right after it uses stale geometry. Concretely
  // this showed up as a second, empty, wrongly-sized bordered box left
  // behind below a tiled window's real content -- resize_border()'s
  // border_bottom/border_right rects, positioned off the OLD width/height
  // that was still current at the moment relayout() called them. Doing it
  // here instead, after the client's own commit lands, keeps geometry and
  // border in sync on every resize, tiled or not.
  view->resize_border();
}

static void xdg_toplevel_request_move(wl_listener* listener, void*) {
  (void)listener;  // Phase 1 scope: floating drag-move.
}

static void xdg_toplevel_request_resize(wl_listener* listener, void*) {
  (void)listener;  // Phase 1 scope: floating interactive resize.
}

static void xdg_toplevel_request_fullscreen(wl_listener* listener, void*) {
  View* view = wl_container_of(listener, view, request_fullscreen);
  // wlroots updates toplevel->requested.fullscreen before firing this
  // event (there's no useful payload in `data` itself) -- see
  // wlr_xdg_shell.h's wlr_xdg_toplevel_requested doc comment.
  view->set_fullscreen(view->xdg_toplevel->requested.fullscreen);
}

// Owns the one commit listener a toplevel's xdg_popup needs to actually
// finish configuring -- see PopupHandle::on_commit below for why this is
// required at all (not just cosmetic scene-node setup like
// layer_surface_new_popup gets away with for the launcher, which never
// triggers this path).
struct PopupHandle {
  Server* server;
  wlr_xdg_popup* popup;
  wl_listener commit{};
  wl_listener destroy{};
};

static void popup_handle_destroy(wl_listener* listener, void*) {
  PopupHandle* handle = wl_container_of(listener, handle, destroy);
  wl_list_remove(&handle->commit.link);
  wl_list_remove(&handle->destroy.link);
  delete handle;
}

static void popup_handle_commit(wl_listener* listener, void*) {
  PopupHandle* handle = wl_container_of(listener, handle, commit);
  // wlr_xdg_popup_unconstrain_from_box() is what actually finishes the
  // popup's configure sequence (it calls wlr_xdg_surface_schedule_configure
  // internally) -- without calling it at all, GTK's dropdown/color-picker
  // popups sat forever waiting for a configure that never came, which is
  // why clicking a dropdown looked like the whole app "hung": the main
  // window still responded to the click event itself (hence the focus
  // ring), but the popup surface never finished initializing so nothing
  // ever rendered or received further input. Same initial_commit gate as
  // xdg_toplevel_surface_commit/layer_surface_surface_commit -- calling
  // schedule_configure (transitively, via unconstrain) before that flips
  // true logs "A configure is scheduled for an uninitialized xdg_surface"
  // and the popup still never configures.
  if (!handle->popup->base->initial_commit) {
    return;
  }
  wlr_box output_box{};
  wlr_output_layout_get_box(handle->server->output_layout(), nullptr, &output_box);
  wlr_xdg_popup_unconstrain_from_box(handle->popup, &output_box);
}

static void xdg_toplevel_new_popup(wl_listener* listener, void* data) {
  View* view = wl_container_of(listener, view, new_popup);
  auto* popup = static_cast<wlr_xdg_popup*>(data);
  // Parent the popup's scene node into the toplevel's own tree so
  // dropdown menus, color pickers, etc. (GtkDropDown/GtkColorButton in
  // fleetwm-settings) stack correctly above the window's own content.
  wlr_scene_xdg_surface_create(view->scene_tree, popup->base);

  auto* handle = new PopupHandle{view->server, popup};
  handle->commit.notify = popup_handle_commit;
  wl_signal_add(&popup->base->surface->events.commit, &handle->commit);
  handle->destroy.notify = popup_handle_destroy;
  wl_signal_add(&popup->base->events.destroy, &handle->destroy);
}

void server_new_xdg_toplevel(wl_listener* listener, void* data) {
  Server* server = wl_container_of(listener, server, new_xdg_toplevel_);
  auto* toplevel = static_cast<wlr_xdg_toplevel*>(data);

  auto view = std::make_unique<View>(server, View::Kind::XdgToplevel);
  view->xdg_toplevel = toplevel;

  // container_tree wraps the actual surface content (scene_tree) plus
  // four border rects framing it -- see view.hpp for why positioning/
  // pin-reparenting acts on container_tree while hit-testing still
  // targets scene_tree. Border rects start fully transparent
  // (kNoBorderColor) and zero-sized; View::resize_border() gives them
  // real dimensions once the surface's actual size is known at map time,
  // and View::set_pinned() is what makes them visible.
  view->container_tree = wlr_scene_tree_create(server->layer_toplevels());
  view->scene_tree = wlr_scene_xdg_surface_create(view->container_tree, toplevel->base);
  view->scene_tree->node.data = view.get();
  toplevel->base->data = view->scene_tree;

  constexpr float kTransparent[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  view->border_top = wlr_scene_rect_create(view->container_tree, 0, 0, kTransparent);
  view->border_bottom = wlr_scene_rect_create(view->container_tree, 0, 0, kTransparent);
  view->border_left = wlr_scene_rect_create(view->container_tree, 0, 0, kTransparent);
  view->border_right = wlr_scene_rect_create(view->container_tree, 0, 0, kTransparent);

  view->map.notify = xdg_toplevel_map;
  wl_signal_add(&toplevel->base->surface->events.map, &view->map);
  view->unmap.notify = xdg_toplevel_unmap;
  wl_signal_add(&toplevel->base->surface->events.unmap, &view->unmap);
  view->destroy.notify = xdg_toplevel_destroy;
  wl_signal_add(&toplevel->events.destroy, &view->destroy);
  view->request_move.notify = xdg_toplevel_request_move;
  wl_signal_add(&toplevel->events.request_move, &view->request_move);
  view->request_resize.notify = xdg_toplevel_request_resize;
  wl_signal_add(&toplevel->events.request_resize, &view->request_resize);
  view->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
  wl_signal_add(&toplevel->events.request_fullscreen, &view->request_fullscreen);
  view->surface_commit.notify = xdg_toplevel_surface_commit;
  wl_signal_add(&toplevel->base->surface->events.commit, &view->surface_commit);
  view->new_popup.notify = xdg_toplevel_new_popup;
  wl_signal_add(&toplevel->base->events.new_popup, &view->new_popup);

  server->views.push_front(std::move(view));
}

// -- xdg-decoration -----------------------------------------------------

void server_new_toplevel_decoration(wl_listener*, void* data) {
  auto* decoration = static_cast<wlr_xdg_toplevel_decoration_v1*>(data);
  // fleetwm draws no decorations of its own -- forcing SERVER_SIDE here
  // just tells the client not to draw its own CSDs (titlebar, buttons),
  // since as far as the protocol is concerned the compositor is now
  // responsible for them. No request_mode listener needed: we always
  // force this regardless of what the client requests, so there's
  // nothing to react to.
  wlr_xdg_toplevel_decoration_v1_set_mode(decoration,
                                           WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

// -- layer-shell surfaces ---------------------------------------------------

void server_new_layer_surface(wl_listener* listener, void* data) {
  Server* server = wl_container_of(listener, server, new_layer_surface_);
  auto* layer_surface = static_cast<wlr_layer_surface_v1*>(data);

  // No output requested -> pick the first output, matching the existing
  // "first output" convention used for View placement (xdg_toplevel_map
  // above). Genuine multi-output targeting is Phase 1+ scope.
  if (!layer_surface->output) {
    if (server->outputs.empty()) {
      wlr_layer_surface_v1_destroy(layer_surface);
      return;
    }
    layer_surface->output = server->outputs.front()->wlr_output_ptr;
  }

  wlr_scene_tree* parent = server->layer_tree_for(layer_surface->pending.layer);

  auto ls = std::make_unique<LayerSurface>(server, layer_surface);
  ls->scene_layer_surface = wlr_scene_layer_surface_v1_create(parent, layer_surface);
  ls->scene_layer_surface->tree->node.data = ls.get();
  layer_surface->data = ls.get();

  ls->map.notify = layer_surface_map;
  wl_signal_add(&layer_surface->surface->events.map, &ls->map);
  ls->unmap.notify = layer_surface_unmap;
  wl_signal_add(&layer_surface->surface->events.unmap, &ls->unmap);
  ls->destroy.notify = layer_surface_destroy;
  wl_signal_add(&layer_surface->events.destroy, &ls->destroy);
  ls->new_popup.notify = layer_surface_new_popup;
  wl_signal_add(&layer_surface->events.new_popup, &ls->new_popup);

  // Cannot configure yet: read from wlroots 0.18.2 source on fleetwm-dev
  // (types/wlr_layer_shell_v1.c) -- wlr_layer_surface_v1_configure()
  // asserts surface->initialized, which layer_surface_role_commit() only
  // sets to true from inside the surface's own first wl_surface.commit
  // handler. At new_surface time (here) it is always still false, so any
  // configure call in this function unconditionally hits "A configure is
  // sent to an uninitialized wlr_layer_surface_v1" -- the header doc's
  // "configure it here" is aspirational, not literal. Must defer to
  // layer_surface_surface_commit, gated on initial_commit.
  ls->surface_commit.notify = layer_surface_surface_commit;
  wl_signal_add(&layer_surface->surface->events.commit, &ls->surface_commit);

  server->layer_surfaces.push_front(std::move(ls));
}

// -- input devices ---------------------------------------------------------

void server_new_input(wl_listener* listener, void* data) {
  Server* server = wl_container_of(listener, server, new_input_);
  auto* device = static_cast<wlr_input_device*>(data);

  if (device->type == WLR_INPUT_DEVICE_KEYBOARD) {
    wlr_keyboard* wlr_kb = wlr_keyboard_from_input_device(device);
    new Keyboard(server, wlr_kb);  // owns itself; freed on its destroy event
  } else if (device->type == WLR_INPUT_DEVICE_POINTER) {
    wlr_cursor_attach_input_device(server->cursor_, device);
  }
}

void server_new_virtual_pointer(wl_listener* listener, void* data) {
  Server* server = wl_container_of(listener, server, new_virtual_pointer_);
  auto* event = static_cast<wlr_virtual_pointer_v1_new_pointer_event*>(data);
  wlr_cursor_attach_input_device(server->cursor_, &event->new_pointer->pointer.base);
}

void server_new_virtual_keyboard(wl_listener* listener, void* data) {
  Server* server = wl_container_of(listener, server, new_virtual_keyboard_);
  auto* virtual_keyboard = static_cast<wlr_virtual_keyboard_v1*>(data);
  new Keyboard(server, &virtual_keyboard->keyboard);  // owns itself; see server_new_input
}

// -- cursor ------------------------------------------------------------

// Walks up from a wlr_scene_node_at() hit to the nearest scene-tree
// ancestor with non-null node.data, and reports which kind of object
// owns it (SceneNodeOwner tag, see scene_node_owner.hpp) without
// reinterpreting the pointer -- callers cast to the right type themselves
// based on `owner`.
struct SceneHit {
  SceneNodeOwner owner;
  void* data;
  wlr_surface* surface;
};

static bool scene_node_at(Server* server, double lx, double ly, double* sx, double* sy,
                           SceneHit* out) {
  wlr_scene_node* node = wlr_scene_node_at(&server->scene()->tree.node, lx, ly, sx, sy);
  if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
    return false;
  }
  wlr_scene_tree* tree = node->parent;
  while (tree && !tree->node.data) {
    tree = tree->node.parent;
  }
  if (!tree) {
    return false;
  }
  out->owner = *static_cast<SceneNodeOwner*>(tree->node.data);
  out->data = tree->node.data;
  // The hit buffer node itself may be a popup or subsurface nested under
  // a View/LayerSurface's tree, not that tree's own main surface -- e.g.
  // a GtkMenuButton popover's scene node is parented under the bar's
  // layer-surface tree (see layer_surface_new_popup), so walking up to
  // "the nearest tagged ancestor" and using ITS main surface routes every
  // click inside the popup to the bar's main surface instead, at the
  // wrong local coordinates. wlr_scene_surface_try_from_buffer() resolves
  // the surface actually backing the hit buffer node, popup or not; only
  // fall back to the tagged ancestor's main surface (a plain view/layer
  // surface with no popup involved) when the hit node has no surface of
  // its own.
  wlr_scene_buffer* scene_buffer = wlr_scene_buffer_from_node(node);
  wlr_scene_surface* scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
  if (scene_surface) {
    out->surface = scene_surface->surface;
  } else if (out->owner == SceneNodeOwner::View) {
    out->surface = static_cast<View*>(tree->node.data)->surface();
  } else {
    out->surface = static_cast<LayerSurface*>(tree->node.data)->surface();
  }
  return true;
}

static void process_cursor_motion(Server* server, uint32_t time_msec) {
  double sx, sy;
  SceneHit hit{};
  bool hit_something = scene_node_at(server, server->cursor()->x, server->cursor()->y, &sx, &sy, &hit);

  if (!hit_something || hit.owner != SceneNodeOwner::View) {
    server->set_default_cursor_image();
  }

  if (hit_something) {
    wlr_seat_pointer_notify_enter(server->seat(), hit.surface, sx, sy);
    wlr_seat_pointer_notify_motion(server->seat(), time_msec, sx, sy);
  } else {
    wlr_seat_pointer_clear_focus(server->seat());
  }

  // Focus-follows-mouse: hovering a view focuses it, no click required.
  // Mirrors server_cursor_button's hit-testing; bare background/layer-shell
  // hits intentionally leave the last-focused view focused (dwm-style, no
  // debounce needed).
  if (hit_something && hit.owner == SceneNodeOwner::View) {
    server->focus_view(static_cast<View*>(hit.data));
  }
}

void server_cursor_motion(wl_listener* listener, void* data) {
  Server* server = wl_container_of(listener, server, cursor_motion_);
  auto* event = static_cast<wlr_pointer_motion_event*>(data);
  wlr_cursor_move(server->cursor_, &event->pointer->base, event->delta_x, event->delta_y);
  process_cursor_motion(server, event->time_msec);
}

void server_cursor_motion_absolute(wl_listener* listener, void* data) {
  Server* server = wl_container_of(listener, server, cursor_motion_absolute_);
  auto* event = static_cast<wlr_pointer_motion_absolute_event*>(data);
  wlr_cursor_warp_absolute(server->cursor_, &event->pointer->base, event->x, event->y);
  process_cursor_motion(server, event->time_msec);
}

void server_cursor_button(wl_listener* listener, void* data) {
  Server* server = wl_container_of(listener, server, cursor_button_);
  auto* event = static_cast<wlr_pointer_button_event*>(data);
  wlr_seat_pointer_notify_button(server->seat(), event->time_msec, event->button, event->state);

  if (event->state != WL_POINTER_BUTTON_STATE_PRESSED) {
    return;
  }
  double sx, sy;
  SceneHit hit{};
  if (scene_node_at(server, server->cursor()->x, server->cursor()->y, &sx, &sy, &hit) &&
      hit.owner == SceneNodeOwner::View) {
    server->focus_view(static_cast<View*>(hit.data));
  }
  // LayerSurface: no click-to-raise/activate needed -- it's already top
  // of its own layer, and keyboard focus (if requested) was already
  // granted on map (see layer_surface_map in layer_surface.cpp), not on
  // click. Pointer button events themselves are already forwarded to the
  // focused client above via wlr_seat_pointer_notify_button regardless of
  // owner kind.
}

void server_cursor_axis(wl_listener* listener, void* data) {
  Server* server = wl_container_of(listener, server, cursor_axis_);
  auto* event = static_cast<wlr_pointer_axis_event*>(data);
  wlr_seat_pointer_notify_axis(server->seat(), event->time_msec, event->orientation,
                                event->delta, event->delta_discrete, event->source,
                                event->relative_direction);
}

void server_cursor_frame(wl_listener* listener, void*) {
  Server* server = wl_container_of(listener, server, cursor_frame_);
  wlr_seat_pointer_notify_frame(server->seat());
}

void server_request_cursor(wl_listener* listener, void* data) {
  Server* server = wl_container_of(listener, server, request_cursor_);
  auto* event = static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);
  wlr_seat_client* focused = server->seat()->pointer_state.focused_client;
  if (focused == event->seat_client) {
    wlr_cursor_set_surface(server->cursor_, event->surface, event->hotspot_x, event->hotspot_y);
  }
}

void server_request_set_selection(wl_listener* listener, void* data) {
  Server* server = wl_container_of(listener, server, request_set_selection_);
  auto* event = static_cast<wlr_seat_request_set_selection_event*>(data);
  wlr_seat_set_selection(server->seat(), event->source, event->serial);
}

Server::Server() = default;

Server::~Server() {
  if (theme_watch_source_) {
    wl_event_source_remove(theme_watch_source_);
  }
  if (theme_watch_fd_ >= 0) {
    close(theme_watch_fd_);
  }
  if (sigterm_source_) {
    wl_event_source_remove(sigterm_source_);
  }
  if (sigint_source_) {
    wl_event_source_remove(sigint_source_);
  }
  if (display_) {
    wl_display_destroy_clients(display_);
    wl_display_destroy(display_);
  }
}

namespace {

// Autostarts one long-lived session helper (fleetwm-bar) once the
// compositor's Wayland socket is up. Same fork+execlp shape as
// input.cpp's keybind-triggered spawn() (kept as a separate local copy
// rather than sharing a header for one function -- matches this file's
// existing style of small per-file free-function helpers, e.g.
// server_theme_watch_readable). A failed exec logs and the child exits;
// it does not affect the compositor itself either way.
//
// Guards against a duplicate instance first: an autostarted helper is
// forked as a child of the compositor, but killing the compositor
// (this project's usual dev-cycle restart, e.g. `pkill -f
// '^/usr/local/bin/fleetwm$'`) does not kill its children -- an old
// instance from a previous compositor run would be orphaned (reparented
// to PID 1) and keep running against a dead Wayland socket, then a
// second one would spawn on the next login. Checks via `pgrep -f
// <full path>` (not `-x <basename>`) for two reasons: `-x` matches
// against the kernel's 15-character-truncated comm name, which silently
// never matches "fleetwm-wallpaper" (17 chars) or any future autostart
// target that long; matching the full absolute path via `-f` instead of
// a bare basename avoids the self-match footgun `pkill -f
// 'fleetwm-bar'` hit earlier this session (a bare basename can appear
// inside this very process's own argv/environment).
bool already_running(const char* full_path) {
  pid_t pid = fork();
  if (pid < 0) {
    return false;  // can't tell; fall through and spawn anyway
  }
  if (pid == 0) {
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
      dup2(devnull, STDOUT_FILENO);
    }
    execlp("pgrep", "pgrep", "-f", full_path, nullptr);
    _exit(1);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// `name` is the bare binary name (resolved via this process's own PATH,
// same as input.cpp's keybind spawn()); `full_path` is the expected
// absolute installed path, used only for the already_running() dedup
// check -- passed separately rather than derived from `name` since
// relying on `pgrep -f <bare name>` to somehow avoid ambiguity is
// exactly the footgun already hit once this session.
void spawn_autostart(const char* name, const char* full_path) {
  if (already_running(full_path)) {
    return;
  }
  pid_t pid = fork();
  if (pid < 0) {
    wlr_log(WLR_ERROR, "fleetwm: fork for autostart '%s' failed", name);
    return;
  }
  if (pid == 0) {
    execlp(name, name, nullptr);
    std::fprintf(stderr, "fleetwm: failed to exec autostart '%s': %s\n", name,
                 std::strerror(errno));
    _exit(1);
  }
}

}  // namespace

void Server::request_lock() {
  if (locked_) {
    return;  // already locked; don't spawn a second fleetwm-locker on top
  }
  pid_t pid = fork();
  if (pid < 0) {
    wlr_log(WLR_ERROR, "fleetwm: fork for fleetwm-locker failed: %s", std::strerror(errno));
    return;
  }
  if (pid == 0) {
    execlp("fleetwm-locker", "fleetwm-locker", nullptr);
    std::fprintf(stderr, "fleetwm: failed to exec fleetwm-locker: %s\n", std::strerror(errno));
    _exit(1);
  }
  locked_ = true;
  locker_pid_ = pid;
}

bool Server::confirm_unlock(pid_t requesting_pid) {
  if (!locked_ || requesting_pid != locker_pid_) {
    return false;
  }
  locked_ = false;
  locker_pid_ = -1;
  return true;
}

bool Server::init() {
  // WLR_DEBUG logs every single cursor motion and scene/render commit --
  // real per-frame CPU cost (string formatting + a session-log write on
  // every one, confirmed via fleetwm-session.log filling with repeated
  // "Falling back to software cursor"-class spam during nothing more
  // than normal mouse movement) that was only ever meant to diagnose one
  // specific bug (a "Lost connection to Wayland compositor" failure when
  // fleetwm-launcher connected). That bug has long since been fixed --
  // the launcher has worked reliably in every session since -- so this
  // is reverted to WLR_INFO, wlroots' own normal-operation default (still
  // logs real problems, just not a debug trace of everything the
  // compositor does every frame). Part of the standing CPU/memory
  // efficiency goal, not a one-off cleanup.
  wlr_log_init(WLR_INFO, nullptr);

  display_ = wl_display_create();

  backend_ = wlr_backend_autocreate(wl_display_get_event_loop(display_), nullptr);
  if (!backend_) {
    return false;
  }

  renderer_ = wlr_renderer_autocreate(backend_);
  if (!renderer_) {
    return false;
  }
  wlr_renderer_init_wl_display(renderer_, display_);

  allocator_ = wlr_allocator_autocreate(backend_, renderer_);
  if (!allocator_) {
    return false;
  }

  compositor_ = wlr_compositor_create(display_, 6, renderer_);
  wlr_subcompositor_create(display_);
  wlr_data_device_manager_create(display_);

  output_layout_ = wlr_output_layout_create(display_);

  scene_ = wlr_scene_create();
  scene_layout_ = wlr_scene_attach_output_layout(scene_, output_layout_);

  // Always-enabled z-order layers, bottom to top -- see server.hpp for the
  // full rationale. Creation order alone establishes correct paint order
  // (wlr_scene_tree_create appends to its parent's child list).
  layer_background_ = wlr_scene_tree_create(&scene_->tree);
  layer_bottom_ = wlr_scene_tree_create(&scene_->tree);
  layer_toplevels_ = wlr_scene_tree_create(&scene_->tree);
  layer_pinned_ = wlr_scene_tree_create(&scene_->tree);
  layer_top_ = wlr_scene_tree_create(&scene_->tree);
  layer_fullscreen_ = wlr_scene_tree_create(&scene_->tree);
  layer_overlay_ = wlr_scene_tree_create(&scene_->tree);

  new_output_.notify = server_new_output;
  wl_signal_add(&backend_->events.new_output, &new_output_);

  xdg_shell_ = wlr_xdg_shell_create(display_, 3);
  new_xdg_toplevel_.notify = server_new_xdg_toplevel;
  wl_signal_add(&xdg_shell_->events.new_toplevel, &new_xdg_toplevel_);

  decoration_manager_ = wlr_xdg_decoration_manager_v1_create(display_);
  new_toplevel_decoration_.notify = server_new_toplevel_decoration;
  wl_signal_add(&decoration_manager_->events.new_toplevel_decoration,
                &new_toplevel_decoration_);

  layer_shell_ = wlr_layer_shell_v1_create(display_, 4);
  new_layer_surface_.notify = server_new_layer_surface;
  wl_signal_add(&layer_shell_->events.new_surface, &new_layer_surface_);

  screencopy_manager_ = wlr_screencopy_manager_v1_create(display_);
  xdg_output_manager_ = wlr_xdg_output_manager_v1_create(display_, output_layout_);

  cursor_ = wlr_cursor_create();
  wlr_cursor_attach_output_layout(cursor_, output_layout_);
  cursor_mgr_ = wlr_xcursor_manager_create(nullptr, 24);

  virtual_pointer_manager_ = wlr_virtual_pointer_manager_v1_create(display_);
  new_virtual_pointer_.notify = server_new_virtual_pointer;
  wl_signal_add(&virtual_pointer_manager_->events.new_virtual_pointer, &new_virtual_pointer_);

  virtual_keyboard_manager_ = wlr_virtual_keyboard_manager_v1_create(display_);
  new_virtual_keyboard_.notify = server_new_virtual_keyboard;
  wl_signal_add(&virtual_keyboard_manager_->events.new_virtual_keyboard, &new_virtual_keyboard_);

  cursor_motion_.notify = server_cursor_motion;
  wl_signal_add(&cursor_->events.motion, &cursor_motion_);
  cursor_motion_absolute_.notify = server_cursor_motion_absolute;
  wl_signal_add(&cursor_->events.motion_absolute, &cursor_motion_absolute_);
  cursor_button_.notify = server_cursor_button;
  wl_signal_add(&cursor_->events.button, &cursor_button_);
  cursor_axis_.notify = server_cursor_axis;
  wl_signal_add(&cursor_->events.axis, &cursor_axis_);
  cursor_frame_.notify = server_cursor_frame;
  wl_signal_add(&cursor_->events.frame, &cursor_frame_);

  new_input_.notify = server_new_input;
  wl_signal_add(&backend_->events.new_input, &new_input_);

  seat_ = wlr_seat_create(display_, "seat0");
  request_cursor_.notify = server_request_cursor;
  wl_signal_add(&seat_->events.request_set_cursor, &request_cursor_);
  request_set_selection_.notify = server_request_set_selection;
  wl_signal_add(&seat_->events.request_set_selection, &request_set_selection_);

#if FLEETWM_XWAYLAND
  // XWayland server startup only for Phase 0: DISPLAY gets set below so X11
  // clients can connect, but there is no new_xwayland_surface listener yet,
  // so mapped X11 surfaces won't produce a View or appear on screen. Wiring
  // that (parallel to the xdg_toplevel path above) is Phase 1 scope,
  // grouped with the master-stack tiling work since both touch View
  // creation/placement.
  xwayland_ = wlr_xwayland_create(display_, compositor_, true);
#endif

  const char* socket = wl_display_add_socket_auto(display_);
  if (!socket) {
    return false;
  }
  setenv("WAYLAND_DISPLAY", socket, true);

  if (!wlr_backend_start(backend_)) {
    return false;
  }

  ipc_server = std::make_unique<IpcServer>(this);
  if (!ipc_server->listen()) {
    wlr_log(WLR_ERROR, "failed to start IPC socket; workspace switching via bar will not work");
  }

  start_signal_handlers();

  theme_config_ = load_theme_config();
  default_apps_config_ = load_default_apps_config();
  reload_keybinds_config();
  if (!start_theme_watch()) {
    wlr_log(WLR_ERROR,
            "failed to start theme.toml inotify watch; live theme reload will not work");
  }

#if FLEETWM_XWAYLAND
  if (xwayland_) {
    setenv("DISPLAY", xwayland_->display_name, true);
  }
#endif

  spawn_autostart("fleetwm-bar", FLEETWM_BINDIR "/fleetwm-bar");
  spawn_autostart("fleetwm-wallpaper", FLEETWM_BINDIR "/fleetwm-wallpaper");

  return true;
}

void Server::run() {
  wl_display_run(display_);
}

void Server::focus_view(View* view) {
  if (!view) {
    wlr_surface* prev_surface = seat_->keyboard_state.focused_surface;
    Output* prev_output = nullptr;
    Workspace* prev_workspace = nullptr;
    if (prev_surface) {
      for (const std::unique_ptr<View>& candidate : views) {
        if (candidate->surface() == prev_surface) {
          candidate->focused = false;
          candidate->resize_border();
          prev_output = candidate->output;
          prev_workspace = candidate->workspace;
          break;
        }
      }
    }
    wlr_seat_keyboard_clear_focus(seat_);
    // Losing focus un-grows this view (grow_at_outer_edges() in
    // output.cpp) -- must run after wlr_seat_keyboard_clear_focus()
    // above so relayout() sees no view as focused anymore.
    if (prev_output) {
      prev_output->relayout();
    }
    raise_always_on_top_views(prev_workspace);
    if (ipc_server) {
      ipc_server->broadcast_focused_title("");
    }
    return;
  }

  wlr_surface* prev_surface = seat_->keyboard_state.focused_surface;
  wlr_surface* surface = view->surface();
  if (prev_surface == surface) {
    return;
  }

  if (prev_surface) {
    wlr_xdg_toplevel* prev_toplevel = wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
    if (prev_toplevel) {
      wlr_xdg_toplevel_set_activated(prev_toplevel, false);
    }
    // Clear the focus border on whichever View previously held focus, if
    // any -- prev_surface alone doesn't identify the owning View, so scan
    // for it the same way focused_view() (input.cpp) does.
    for (const std::unique_ptr<View>& candidate : views) {
      if (candidate->surface() == prev_surface) {
        candidate->focused = false;
        candidate->resize_border();
        break;
      }
    }
  }

  auto it = std::find_if(views.begin(), views.end(),
                          [view](const std::unique_ptr<View>& v) { return v.get() == view; });
  if (it != views.end() && it != views.begin()) {
    views.splice(views.begin(), views, it);  // move to front (topmost) without destroying
  }
  wlr_scene_node_raise_to_top(&view->container_tree->node);
  // Re-assert always-on-top (fleetwm-settings) above whatever was just
  // raised, if it shares this view's workspace -- see the comment on
  // View::always_on_top (view.hpp) and raise_always_on_top_views() above.
  raise_always_on_top_views(view->workspace);

  if (view->kind == View::Kind::XdgToplevel && view->xdg_toplevel) {
    wlr_xdg_toplevel_set_activated(view->xdg_toplevel, true);
  }
  view->focused = true;
  view->resize_border();

  wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat_);
  if (keyboard) {
    wlr_seat_keyboard_notify_enter(seat_, surface, keyboard->keycodes, keyboard->num_keycodes,
                                    &keyboard->modifiers);
  }

  // Newly-focused view "steps forward" a few px (grow_at_outer_edges() in
  // output.cpp) -- relayout() recomputes every tiled view's box on this
  // output, so both this view and whichever previously-grown one is now
  // back to normal size get updated in the same pass. Must run after
  // wlr_seat_keyboard_notify_enter() above: relayout() resolves "the
  // focused view" from the seat's own focused_surface, which that call
  // is what actually updates. No-op if `view` is floating/pinned
  // (relayout() only touches tiled views).
  if (view->output) {
    view->output->relayout();
  }

  if (ipc_server) {
    std::string title;
    if (view->kind == View::Kind::XdgToplevel && view->xdg_toplevel) {
      if (view->xdg_toplevel->title) {
        title = view->xdg_toplevel->title;
      } else if (view->xdg_toplevel->app_id) {
        title = view->xdg_toplevel->app_id;
      }
    }
    ipc_server->broadcast_focused_title(title);
  }
}

void Server::focus_layer_surface(LayerSurface* layer_surface) {
  wlr_surface* surface = layer_surface->surface();
  wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat_);
  wlr_seat_keyboard_notify_enter(seat_, surface, keyboard ? keyboard->keycodes : nullptr,
                                  keyboard ? keyboard->num_keycodes : 0,
                                  keyboard ? &keyboard->modifiers : nullptr);
}

wlr_scene_tree* Server::layer_tree_for(zwlr_layer_shell_v1_layer layer) {
  switch (layer) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
      return layer_background_;
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
      return layer_bottom_;
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
      return layer_top_;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
      return layer_overlay_;
  }
  return layer_overlay_;
}

void Server::set_default_cursor_image() {
  wlr_cursor_set_xcursor(cursor_, cursor_mgr_, "left_ptr");
}

namespace {

// Resolves a keybinds.toml key name to an xkb_keysym_t via the same
// libxkbcommon name table xkb itself uses (so "Return", "d", "Q",
// "Escape" etc. all just work, matching how the value would be written
// in an xkb keymap). Falls back to the given default and logs a warning
// on an unresolvable name (typo, or a name libxkbcommon doesn't
// recognize) rather than silently disabling the bind with no
// explanation.
xkb_keysym_t resolve_keybind(const std::string& name, xkb_keysym_t fallback,
                              const char* field_name) {
  if (name.empty()) {
    return fallback;
  }
  xkb_keysym_t sym = xkb_keysym_from_name(name.c_str(), XKB_KEYSYM_NO_FLAGS);
  if (sym == XKB_KEY_NoSymbol) {
    wlr_log(WLR_ERROR, "fleetwm: keybinds.toml: unrecognized key name '%s' for '%s', keeping default",
            name.c_str(), field_name);
    return fallback;
  }
  return sym;
}

}  // namespace

void Server::reload_keybinds_config() {
  keybinds_config_ = load_keybinds_config();
  ResolvedKeybinds defaults;  // XKB_KEY_* defaults, see server.hpp
  resolved_keybinds_.terminal =
      resolve_keybind(keybinds_config_.terminal, defaults.terminal, "terminal");
  resolved_keybinds_.launcher =
      resolve_keybind(keybinds_config_.launcher, defaults.launcher, "launcher");
  resolved_keybinds_.close_window =
      resolve_keybind(keybinds_config_.close_window, defaults.close_window, "close_window");
  resolved_keybinds_.toggle_pin =
      resolve_keybind(keybinds_config_.toggle_pin, defaults.toggle_pin, "toggle_pin");
  resolved_keybinds_.toggle_float =
      resolve_keybind(keybinds_config_.toggle_float, defaults.toggle_float, "toggle_float");
  resolved_keybinds_.lock = resolve_keybind(keybinds_config_.lock, defaults.lock, "lock");
  resolved_keybinds_.screenshot =
      resolve_keybind(keybinds_config_.screenshot, defaults.screenshot, "screenshot");
  resolved_keybinds_.focus_left =
      resolve_keybind(keybinds_config_.focus_left, defaults.focus_left, "focus_left");
  resolved_keybinds_.focus_down =
      resolve_keybind(keybinds_config_.focus_down, defaults.focus_down, "focus_down");
  resolved_keybinds_.focus_up =
      resolve_keybind(keybinds_config_.focus_up, defaults.focus_up, "focus_up");
  resolved_keybinds_.focus_right =
      resolve_keybind(keybinds_config_.focus_right, defaults.focus_right, "focus_right");
  resolved_keybinds_.quit = resolve_keybind(keybinds_config_.quit, defaults.quit, "quit");
}

void Server::reload_theme_config() {
  theme_config_ = load_theme_config();
  for (const std::unique_ptr<View>& view : views) {
    view->resize_border();
  }
  // gap_px lives on ThemeConfig too, so a live theme reload must re-tile
  // every output, not just refresh border rects.
  for (const std::unique_ptr<Output>& output : outputs) {
    output->relayout();
  }
}

int server_theme_watch_readable(int fd, uint32_t, void* data) {
  auto* server = static_cast<Server*>(data);
  // inotify events arrive batched in one read(); a single save can emit
  // more than one (e.g. a CLOSE_WRITE plus a separate MOVED_TO for an
  // atomic rename-into-place save), so drain everything available and
  // reload once rather than once per event -- reload_theme_config() is
  // cheap and idempotent, but there's no reason to redo it several
  // times for what the user experienced as one save.
  alignas(struct inotify_event) char buf[4096];
  bool got_theme_event = false;
  bool got_default_apps_event = false;
  bool got_keybinds_event = false;
  ssize_t n;
  while ((n = read(fd, buf, sizeof(buf))) > 0) {
    ssize_t offset = 0;
    while (offset < n) {
      auto* event = reinterpret_cast<struct inotify_event*>(buf + offset);
      if (event->len > 0 && std::strcmp(event->name, "theme.toml") == 0) {
        got_theme_event = true;
      } else if (event->len > 0 && std::strcmp(event->name, "default_apps.toml") == 0) {
        got_default_apps_event = true;
      } else if (event->len > 0 && std::strcmp(event->name, "keybinds.toml") == 0) {
        got_keybinds_event = true;
      }
      offset += static_cast<ssize_t>(sizeof(struct inotify_event)) + event->len;
    }
  }
  if (got_theme_event) {
    server->reload_theme_config();
  }
  if (got_default_apps_event) {
    server->reload_default_apps_config();
  }
  if (got_keybinds_event) {
    server->reload_keybinds_config();
  }
  return 0;
}

namespace {

int server_signal_terminate(int, void* data) {
  auto* server = static_cast<Server*>(data);
  wl_display_terminate(server->display());
  return 0;
}

}  // namespace

void Server::start_signal_handlers() {
  wl_event_loop* loop = wl_display_get_event_loop(display_);
  sigterm_source_ = wl_event_loop_add_signal(loop, SIGTERM, server_signal_terminate, this);
  sigint_source_ = wl_event_loop_add_signal(loop, SIGINT, server_signal_terminate, this);
  if (sigterm_source_ == nullptr || sigint_source_ == nullptr) {
    wlr_log(WLR_ERROR, "failed to register SIGTERM/SIGINT handlers; kill will not shut down "
                        "cleanly (clients won't be notified, atexit hooks won't run)");
  }
}

bool Server::start_theme_watch() {
  theme_watch_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (theme_watch_fd_ < 0) {
    return false;
  }

  // Watch theme.toml's parent directory rather than the file itself:
  // save_theme_config() (theme.cpp) writes via a fresh std::ofstream each
  // call, and the directory needs to already exist (fs::create_directories
  // there) before the first save ever happens -- watching the directory
  // means the watch survives across saves regardless of exactly how each
  // one lands on disk (in-place write vs. a tool that writes-then-renames).
  std::filesystem::path config_dir = std::filesystem::path(user_config_path()).parent_path();
  std::filesystem::create_directories(config_dir);

  int wd = inotify_add_watch(theme_watch_fd_, config_dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO);
  if (wd < 0) {
    close(theme_watch_fd_);
    theme_watch_fd_ = -1;
    return false;
  }

  wl_event_loop* loop = wl_display_get_event_loop(display_);
  theme_watch_source_ = wl_event_loop_add_fd(loop, theme_watch_fd_, WL_EVENT_READABLE,
                                              server_theme_watch_readable, this);
  return theme_watch_source_ != nullptr;
}

void Server::notify_keyboard_added() {
  ++keyboard_count_;
  uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
  if (keyboard_count_ > 0) {
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
  }
  wlr_seat_set_capabilities(seat_, caps);
}

void Server::notify_keyboard_removed() {
  --keyboard_count_;
  uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
  if (keyboard_count_ > 0) {
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
  }
  wlr_seat_set_capabilities(seat_, caps);
}

Workspace* Server::active_workspace_for_focused_output() {
  if (outputs.empty()) {
    return nullptr;
  }
  // Phase 0 has one implicit "focused output" (the first one) until Phase 1
  // adds real focus-follows-cursor output tracking for multi-monitor setups.
  return &outputs.front()->active_workspace();
}

Output* Server::output_for(wlr_output* wlr_output_ptr) const {
  for (const std::unique_ptr<Output>& output : outputs) {
    if (output->wlr_output_ptr == wlr_output_ptr) {
      return output.get();
    }
  }
  return nullptr;
}

}  // namespace fleetwm
