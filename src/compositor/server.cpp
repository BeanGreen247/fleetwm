#include "server.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/inotify.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-server-core.h>

extern "C" {
#include <libinput.h>
#include <wlr/backend/libinput.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/render/pixman.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/util/log.h>
}

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "app_appearance.hpp"
#include "input.hpp"
#include "ipc_server.hpp"
#include "layer_surface.hpp"
#include "popup_namespaces.hpp"
#include "malloc_tuning.hpp"
#include "output.hpp"
#include "paths_config.h"
#include "scene_node_owner.hpp"
#include "cursor.hpp"
#include "titlebar.hpp"
#include "window_geometry.hpp"
#include "view.hpp"

namespace fleetwm {

// These trampolines are declared as friends of Server (see server.hpp) so
// they must live directly in the fleetwm namespace, not a nested anonymous
// namespace -- friend declarations name fleetwm::server_new_output etc.
// exactly, and an anonymous-namespace definition is a distinct entity that
// friendship would not reach.

// -- output ------------------------------------------------------------

namespace {

// Chooses the mode to use for `out` given a saved setting: an exact size
// (and refresh, if one was saved) match, else the highest refresh rate of
// that size, else the monitor's preferred mode.
wlr_output_mode* pick_mode(wlr_output* out, const OutputSetting* setting) {
  wlr_output_mode* preferred = wlr_output_preferred_mode(out);
  if (!setting || setting->width <= 0 || setting->height <= 0) {
    return preferred;
  }
  wlr_output_mode* best = nullptr;
  for (wl_list* l = out->modes.next; l != &out->modes; l = l->next) {
    wlr_output_mode* m = wl_container_of(l, m, link);
    if (m->width != setting->width || m->height != setting->height) {
      continue;
    }
    if (setting->refresh_mhz > 0) {
      if (std::abs(m->refresh - setting->refresh_mhz) <= 500) {
        return m;
      }
      continue;
    }
    if (!best || m->refresh > best->refresh) {
      best = m;
    }
  }
  return best ? best : preferred;
}

}  // namespace

void server_new_output(wl_listener* listener, void* data) {
  Server* server = wl_container_of(listener, server, new_output_);
  auto* wlr_out = static_cast<wlr_output*>(data);

  wlr_output_init_render(wlr_out, server->allocator_, server->renderer_);

  const OutputSetting* setting = nullptr;
  auto saved = server->output_settings_.find(wlr_out->name);
  if (saved != server->output_settings_.end()) {
    setting = &saved->second;
  }

  wlr_output_state state;
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state, true);
  wlr_output_mode* mode = pick_mode(wlr_out, setting);
  if (mode) {
    wlr_output_state_set_mode(&state, mode);
  }
  if (!wlr_output_commit_state(wlr_out, &state) && mode != wlr_output_preferred_mode(wlr_out)) {
    // The saved mode was rejected: fall back to the monitor's preferred one
    // rather than leaving the output dark.
    wlr_output_state_finish(&state);
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    if (wlr_output_mode* preferred = wlr_output_preferred_mode(wlr_out)) {
      wlr_output_state_set_mode(&state, preferred);
    }
    wlr_output_commit_state(wlr_out, &state);
  }
  wlr_output_state_finish(&state);

  auto output = std::make_unique<Output>(server, wlr_out);
  if (setting && setting->has_pos) {
    wlr_output_layout_add(server->output_layout_, wlr_out, setting->x, setting->y);
  } else {
    wlr_output_layout_add_auto(server->output_layout_, wlr_out);
  }
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
  if (server->ipc_server) {
    server->ipc_server->broadcast_outputs_changed();
  }
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

void view_mapped(View* view) {

  view->update_size_policy();
  // Border rects are sized off the surface's real geometry, only known
  // once the client has actually mapped (it picks its own size -- see
  // xdg_toplevel_surface_commit's size-0,0 "client decides" configure).
  view->resize_border();

  // fleetwm-settings always opens floating and centered, on top of
  // whatever's tiled in the workspace it lands in -- explicit user
  // request. It's a single-instance panel, not something that should
  // compete for tiling space the way a terminal would. Matched by its
  // app id (set in settings/main.cpp),
  // not window title, since that's stable regardless of locale/theme.
  bool is_settings = view->kind == View::Kind::XdgToplevel && view->xdg_toplevel &&
                      view->xdg_toplevel->app_id &&
                      (std::strcmp(view->xdg_toplevel->app_id, "dev.fleetwm.Settings") == 0 ||
                       std::strcmp(view->xdg_toplevel->app_id, "dev.fleetwm.Shortcuts") == 0 ||
                       std::strcmp(view->xdg_toplevel->app_id, "dev.fleetwm.LangPicker") == 0);
  if (is_settings) {
    view->set_floating(true);
    view->fleetwm_panel = true;
    // Always on top only in the Tiling layout, where there is no other way to keep it reachable; in
    // the Desktop layout it stacks like any other window (and can be pinned from its titlebar).
    view->always_on_top = !view->server->desktop_layout();
    view->update_stacking_layer();
  }

  // GTK dialogs (GtkColorChooserDialog, GtkFileChooserDialog, etc.) map as
  // their own separate xdg_toplevel with xdg_toplevel.set_parent pointing
  // back at the window that opened them -- they are NOT xdg_popups (no
  // positioner, no implicit grab), so they were previously falling through
  // to the exact same tiled-window treatment as a terminal or any other
  // regular toplevel: placed wherever the tiling layout put them and
  // stacked in normal z-order, which could leave them appearing behind
  // their own already-on-top parent (e.g. fleetwm-settings' accent-color
  // picker dialog rendering underneath the settings window, confirmed via
  // live testing) instead of centered over it. Any toplevel with a
  // non-null `parent` gets the same floating+always-on-top+centered
  // treatment fleetwm-settings itself gets below, just centered over its
  // parent's geometry rather than the whole output.
  View* dialog_parent = nullptr;
  if (!is_settings && view->is_child_window()) {
    for (const std::unique_ptr<View>& candidate : view->server->views) {
      if (view->kind == View::Kind::XdgToplevel ? candidate->xdg_toplevel == view->xdg_toplevel->parent
#if FLEETWM_XWAYLAND
                                                 : candidate->xwayland_surface == view->xwayland_surface->parent
#else
                                                 : false
#endif
      ) {
        dialog_parent = candidate.get();
        break;
      }
    }
  }
  bool is_dialog = dialog_parent != nullptr;
  if (is_dialog) {
    view->set_floating(true);
    view->always_on_top = true;
    view->update_stacking_layer();
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
      const wlr_box geo = view->content_geometry();
      wlr_scene_node_set_position(&view->container_tree->node, box.x + (box.width - geo.width) / 2,
                                   box.y + (box.height - geo.height) / 2);
      wlr_scene_node_raise_to_top(&view->container_tree->node);
    } else if (is_dialog) {
      // Center over the PARENT's current on-screen box, not the whole
      // output -- a color picker centered on the output rather than on
      // the settings window it belongs to would visually "jump" away from
      // what the user just clicked.
      const wlr_box geo = view->content_geometry();
      int parent_x = 0, parent_y = 0;
      wlr_scene_node_coords(&dialog_parent->container_tree->node, &parent_x, &parent_y);
      const wlr_box parent_geo = dialog_parent->content_geometry();
      wlr_scene_node_set_position(&view->container_tree->node,
                                   parent_x + (parent_geo.width - geo.width) / 2,
                                   parent_y + (parent_geo.height - geo.height) / 2);
      wlr_scene_node_raise_to_top(&view->container_tree->node);
    } else if (view->desktop_mode() && view->is_window() && !view->pinned) {
      // Desktop layout: windows are free-floating. Open at the client's own
      // size, centered on the work area and stepped down-right per open
      // window so a stack of new windows stays readable.
      const wlr_box geo = view->content_geometry();
      const int outer_w = (geo.width > 0 ? geo.width : 800) + 2 * view->border_thickness();
      const int outer_h = (geo.height > 0 ? geo.height : 500) + view->titlebar_height() + 2 * view->border_thickness();
      const wlr_box area = output->usable_area;
      const geom::Box at = geom::cascade_position({area.x, area.y, area.width, area.height}, outer_w,
                                                  outer_h, static_cast<int>(workspace.views().size()) - 1);
      const int x = at.x, y = at.y;
      wlr_scene_node_set_position(&view->container_tree->node, x, y);
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
  view->server->schedule_windows_broadcast();
}

static void xdg_toplevel_map(wl_listener* listener, void*) {
  View* view = wl_container_of(listener, view, map);
  view_mapped(view);
}

void view_unmapped(View* view) {
  Server* server = view->server;
  bool was_focused = server->seat()->keyboard_state.focused_surface == view->surface();
  Output* output = view->output;
  server->forget_view(view);
  server->schedule_windows_broadcast();

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

  // i3/dwm-style focus-on-close: hand focus to the topmost remaining visible
  // view (server->views is front-to-back, see focus_view()'s splice-to-front),
  // or clear focus when none is left.
  server->focus_next_after(view);
}

static void xdg_toplevel_unmap(wl_listener* listener, void*) {
  View* view = wl_container_of(listener, view, unmap);
  view_unmapped(view);
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
  wl_list_remove(&view->request_maximize.link);
  wl_list_remove(&view->set_title.link);
  server->forget_view(view);
  wl_list_remove(&view->request_fullscreen.link);
  wl_list_remove(&view->surface_commit.link);
  wl_list_remove(&view->new_popup.link);
  // request_configure (and the other X11-only listeners) belong to X11 windows, which are torn
  // down in x11_destroy() (xwayland.cpp). They are never added for an xdg toplevel, and
  // wl_list_remove() on a never-added listener dereferences null pointers, so they must
  // not be removed here.

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
  View* view = wl_container_of(listener, view, request_move);
  if (view->server->desktop_layout() && !view->fullscreen) {
    view->server->begin_move(view);
  }
}

static void xdg_toplevel_request_resize(wl_listener* listener, void* data) {
  View* view = wl_container_of(listener, view, request_resize);
  auto* event = static_cast<wlr_xdg_toplevel_resize_event*>(data);
  if (view->server->desktop_layout() && !view->fullscreen) {
    view->server->begin_resize(view, event->edges);
  }
}

static void xdg_toplevel_set_title(wl_listener* listener, void*) {
  View* view = wl_container_of(listener, view, set_title);
  view->update_titlebar();  // a title change does not necessarily come with a commit
  view->server->schedule_windows_broadcast();
}

static void xdg_toplevel_request_maximize(wl_listener* listener, void*) {
  View* view = wl_container_of(listener, view, request_maximize);
  if (view->server->desktop_layout() && view->output) {
    view->set_maximized(view->xdg_toplevel->requested.maximized);
  } else if (view->xdg_toplevel->base->initialized) {
    // Not honoured in the tiling layout, but the protocol wants an answer.
    wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
  }
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
  view->id = server->next_view_id_++;

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

  create_view_rects(view.get());

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
  view->set_title.notify = xdg_toplevel_set_title;
  wl_signal_add(&toplevel->events.set_title, &view->set_title);
  view->request_maximize.notify = xdg_toplevel_request_maximize;
  wl_signal_add(&toplevel->events.request_maximize, &view->request_maximize);
  view->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
  wl_signal_add(&toplevel->events.request_fullscreen, &view->request_fullscreen);
  view->surface_commit.notify = xdg_toplevel_surface_commit;
  wl_signal_add(&toplevel->base->surface->events.commit, &view->surface_commit);
  view->new_popup.notify = xdg_toplevel_new_popup;
  wl_signal_add(&toplevel->base->events.new_popup, &view->new_popup);

  server->views.push_front(std::move(view));
}

// -- xdg-decoration -----------------------------------------------------

void server_new_toplevel_decoration(wl_listener* listener, void* data) {
  Server* server = wl_container_of(listener, server, new_toplevel_decoration_);
  auto* decoration = static_cast<wlr_xdg_toplevel_decoration_v1*>(data);
  // fleetwm draws no decorations of its own -- forcing SERVER_SIDE here
  // just tells the client not to draw its own CSDs (titlebar, buttons),
  // since as far as the protocol is concerned the compositor is now
  // responsible for them. No request_mode listener needed: we always
  // force this regardless of what the client requests, so there's
  // nothing to react to.
  wlr_xdg_toplevel_decoration_v1_set_mode(decoration,
                                           WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
  // Remember that this client expects the compositor to decorate it: the
  // Desktop layout draws a titlebar for such windows.
  for (const std::unique_ptr<View>& view : server->views) {
    if (view->xdg_toplevel == decoration->toplevel) {
      view->has_decoration = true;
      break;
    }
  }
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
    server->apply_mouse_config(device);
    struct Watch {  // forgets the device when it is unplugged
      Server* server;
      wlr_input_device* device;
      wl_listener destroy;
    };
    auto* watch = new Watch{server, device, {}};
    watch->destroy.notify = [](wl_listener* l, void*) {
      Watch* w = wl_container_of(l, w, destroy);
      w->server->forget_mouse_device(w->device);
      wl_list_remove(&w->destroy.link);
      delete w;
    };
    wl_signal_add(&device->events.destroy, &watch->destroy);
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
  new Keyboard(server, &virtual_keyboard->keyboard, true);  // owns itself; see server_new_input
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
  const bool surface_buffer =
      node && node->type == WLR_SCENE_NODE_BUFFER &&
      wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node)) != nullptr;
  if (node && !surface_buffer && node->data &&
      *static_cast<SceneNodeOwner*>(node->data) == SceneNodeOwner::Decoration) {
    out->owner = SceneNodeOwner::Decoration;
    out->data = node->data;
    out->surface = nullptr;
    return true;
  }
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
  } else if (out->owner == SceneNodeOwner::Unmanaged) {
    return false;  // an X11 menu or tooltip always resolves to its own surface above
  } else {
    out->surface = static_cast<LayerSurface*>(tree->node.data)->surface();
  }
  return true;
}

namespace {

// What part of a decorated window the pointer is over.
struct DecorationZone {
  uint32_t edges = 0;  // WLR_EDGE_* mask: a resize handle
  int button = -1;     // TitlebarButton
  bool drag = false;   // titlebar background
};

constexpr int kEdgeInner = 4;   // px inside the window that still count as border
constexpr int kCornerSpan = 12;  // px along an edge that count as the corner

DecorationZone decoration_zone(Server* server, View* view) {
  DecorationZone zone;
  int cx = 0, cy = 0;
  wlr_scene_node_coords(&view->container_tree->node, &cx, &cy);
  const double lx = server->cursor()->x - cx, ly = server->cursor()->y - cy;
  const int bt = view->border_thickness();
  const int th = view->titlebar_height();
  const wlr_box geo = view->content_geometry();
  const int W = view->content_w + 2 * bt;
  const int H = std::max(1, geo.height) + th + 2 * bt;

  uint32_t edges = geom::resize_edges_at(lx, ly, W, H, std::max(kEdgeInner, bt), kCornerSpan);
  if (view->maximized) edges = 0;  // a maximized window is not resized by its edges
  zone.edges = edges;
  if (edges) return zone;

  if (th > 0 && ly >= bt && ly < bt + th && lx >= bt && lx < bt + view->content_w) {
    const geom::TitlebarLayout layout =
        geom::layout_titlebar(view->content_w, titlebar_metrics(server->theme_config().titlebar));
    zone.button = geom::titlebar_button_at(layout, lx - bt, ly - bt);
    zone.drag = zone.button == geom::kBtnNone;
  }
  return zone;
}

const char* resize_cursor_name(uint32_t edges) {
  const bool l = edges & WLR_EDGE_LEFT, r = edges & WLR_EDGE_RIGHT;
  const bool t = edges & WLR_EDGE_TOP, b = edges & WLR_EDGE_BOTTOM;
  if (t && l) return "nw-resize";
  if (t && r) return "ne-resize";
  if (b && l) return "sw-resize";
  if (b && r) return "se-resize";
  if (t) return "n-resize";
  if (b) return "s-resize";
  if (l) return "w-resize";
  if (r) return "e-resize";
  return "left_ptr";
}

}  // namespace

static void process_cursor_motion(Server* server, uint32_t time_msec) {
  if (server->grab_active()) {
    server->update_grab();
    return;
  }
  double sx, sy;
  SceneHit hit{};
  bool hit_something = scene_node_at(server, server->cursor()->x, server->cursor()->y, &sx, &sy, &hit);

  if (hit_something && hit.owner == SceneNodeOwner::Decoration) {
    View* view = static_cast<DecorationTag*>(hit.data)->view;
    if (server->desktop_layout() && view) {
      const DecorationZone zone = decoration_zone(server, view);
      wlr_seat_pointer_clear_focus(server->seat());
      server->set_cursor_name(zone.edges ? resize_cursor_name(zone.edges) : "left_ptr");
      server->set_hover_view(view);
      view->set_hover_button(zone.button);
      return;
    }
    hit_something = false;  // invisible ring left over after a layout switch
  }
  server->set_hover_view(nullptr);

  if (!hit_something || hit.owner != SceneNodeOwner::View) {
    server->set_default_cursor_image();
  }

  if (hit_something) {
    wlr_seat_pointer_notify_enter(server->seat(), hit.surface, sx, sy);
    wlr_seat_pointer_notify_motion(server->seat(), time_msec, sx, sy);
  } else {
    wlr_seat_pointer_clear_focus(server->seat());
  }

  // Focus-follows-mouse (tiling layout only; the desktop layout focuses on
  // click): hovering a view focuses it, no click required. Bare background and
  // layer-shell hits intentionally leave the last-focused view focused.
  if (hit_something && hit.owner == SceneNodeOwner::View && !server->desktop_layout()) {
    server->focus_view(static_cast<View*>(hit.data));
  }
}

void server_cursor_motion(wl_listener* listener, void* data) {
  Server* server = wl_container_of(listener, server, cursor_motion_);
  server->note_activity();
  auto* event = static_cast<wlr_pointer_motion_event*>(data);
  wlr_cursor_move(server->cursor_, &event->pointer->base, event->delta_x, event->delta_y);
  process_cursor_motion(server, event->time_msec);
}

void server_cursor_motion_absolute(wl_listener* listener, void* data) {
  Server* server = wl_container_of(listener, server, cursor_motion_absolute_);
  server->note_activity();
  auto* event = static_cast<wlr_pointer_motion_absolute_event*>(data);
  wlr_cursor_warp_absolute(server->cursor_, &event->pointer->base, event->x, event->y);
  process_cursor_motion(server, event->time_msec);
}

void server_cursor_button(wl_listener* listener, void* data) {
  Server* server = wl_container_of(listener, server, cursor_button_);
  server->note_activity();
  auto* event = static_cast<wlr_pointer_button_event*>(data);

  if (event->state != WL_POINTER_BUTTON_STATE_PRESSED) {
    if (server->grab_active()) {
      server->end_grab();
      server->swallow_release = false;
      return;
    }
    if (server->swallow_release) {
      server->swallow_release = false;
      return;
    }
    wlr_seat_pointer_notify_button(server->seat(), event->time_msec, event->button, event->state);
    return;
  }

  double sx, sy;
  SceneHit hit{};
  const bool hit_something =
      scene_node_at(server, server->cursor()->x, server->cursor()->y, &sx, &sy, &hit);

  // The start menu and the volume mixer are only as big as their card, so a press anywhere else is what closes them. The press is
  // consumed (as when the menu covered the whole output): clicking the Start button again then just
  // closes the menu instead of opening it anew.
  for (const std::unique_ptr<LayerSurface>& ls : server->layer_surfaces) {
    wlr_layer_surface_v1* menu = ls->layer_surface;
    if (!menu->surface->mapped || !dismisses_on_outside_click(menu->namespace_)) continue;
    if (hit_something && hit.surface != nullptr && wlr_surface_get_root_surface(hit.surface) == menu->surface) break;
    wlr_layer_surface_v1_destroy(menu);  // sends "closed"; the popup's program quits
    server->swallow_release = true;
    return;
  }

  // Titlebar / resize ring of a Desktop-layout window: handled here and not
  // forwarded to the client.
  if (hit_something && hit.owner == SceneNodeOwner::Decoration && server->desktop_layout()) {
    View* view = static_cast<DecorationTag*>(hit.data)->view;
    if (view) {
      server->focus_view(view);
      server->swallow_release = true;
      if (event->button == 0x110 /* BTN_LEFT */) {
        const DecorationZone zone = decoration_zone(server, view);
        if (zone.edges) {
          server->begin_resize(view, zone.edges);
        } else if (zone.button == geom::kBtnClose) {
          view->close();
        } else if (zone.button == geom::kBtnMinimize) {
          server->minimize_view(view);
        } else if (zone.button == geom::kBtnPin) {
          view->set_pinned(!view->pinned);
        } else if (zone.button == geom::kBtnMaximize) {
          server->toggle_maximize(view);
        } else if (zone.drag) {
          if (server->is_double_click(view, event->time_msec)) {
            server->toggle_maximize(view);
          } else if (!view->fullscreen) {
            server->begin_move(view);
          }
        }
      }
    }
    return;
  }

  wlr_seat_pointer_notify_button(server->seat(), event->time_msec, event->button, event->state);

  if (hit_something && hit.owner == SceneNodeOwner::View) {
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
  server->note_activity();
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
    server->cursor_name_ = nullptr;  // a client cursor replaces whatever xcursor was set
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
  if (windows_idle_) {
    wl_event_source_remove(windows_idle_);
  }
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
  if (sigchld_source_) {
    wl_event_source_remove(sigchld_source_);
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

pid_t Server::spawn_locker() {
  pid_t pid = fork();
  if (pid < 0) {
    wlr_log(WLR_ERROR, "fleetwm: fork for fleetwm-locker failed: %s", std::strerror(errno));
    return -1;
  }
  if (pid == 0) {
    execlp("fleetwm-locker", "fleetwm-locker", nullptr);
    std::fprintf(stderr, "fleetwm: failed to exec fleetwm-locker: %s\n", std::strerror(errno));
    _exit(1);
  }
  return pid;
}

void Server::request_lock() {
  if (locked_) {
    return;  // already locked; don't spawn a second fleetwm-locker on top
  }
  pid_t pid = spawn_locker();
  if (pid < 0) {
    return;
  }
  locked_ = true;
  locker_pid_ = pid;
  locker_respawns_.clear();
}

void Server::on_child_exited(pid_t pid, int status) {
  if (!locked_ || pid != locker_pid_) {
    return;  // not the lock screen, or it already unlocked properly before exiting
  }
  // The locker exited while the session is still locked: it crashed or was
  // killed. Never unlock on that (it would let anyone unlock by crashing the
  // locker) -- start a fresh lock screen instead, unless it keeps dying.
  wlr_log(WLR_ERROR, "fleetwm: fleetwm-locker (pid %d) exited while locked (status 0x%x); respawning",
          static_cast<int>(pid), status);
  const auto now = std::chrono::steady_clock::now();
  locker_respawns_.erase(std::remove_if(locker_respawns_.begin(), locker_respawns_.end(),
                                        [&](const auto& t) { return now - t > std::chrono::seconds(10); }),
                         locker_respawns_.end());
  if (locker_respawns_.size() >= 5) {
    wlr_log(WLR_ERROR, "fleetwm: fleetwm-locker keeps dying; staying locked without a lock screen "
                       "(recover from another VT or over ssh)");
    locker_pid_ = -1;
    return;
  }
  locker_respawns_.push_back(now);
  locker_pid_ = spawn_locker();
}

bool Server::confirm_unlock(pid_t requesting_pid) {
  if (!locked_ || requesting_pid != locker_pid_) {
    return false;
  }
  locked_ = false;
  locker_pid_ = -1;
  return true;
}

namespace {

bool has_render_node() {
  DIR* dir = opendir("/dev/dri");
  if (!dir) {
    return false;
  }
  bool found = false;
  while (dirent* e = readdir(dir)) {
    if (std::strncmp(e->d_name, "renderD", 7) == 0) {
      found = true;
      break;
    }
  }
  closedir(dir);
  return found;
}

}  // namespace

std::vector<Server::OutputInfo> Server::describe_outputs() const {
  std::vector<OutputInfo> result;
  for (const std::unique_ptr<Output>& output : outputs) {
    wlr_output* wo = output->wlr_output_ptr;
    OutputInfo info;
    info.name = wo->name ? wo->name : "";
    wlr_box box{};
    wlr_output_layout_get_box(output_layout_, wo, &box);
    info.x = box.x;
    info.y = box.y;
    info.width = wo->width;
    info.height = wo->height;
    info.refresh_mhz = wo->refresh;
    for (wl_list* l = wo->modes.next; l != &wo->modes; l = l->next) {
      wlr_output_mode* m = wl_container_of(l, m, link);
      ModeInfo mi;
      mi.width = m->width;
      mi.height = m->height;
      mi.refresh_mhz = m->refresh;
      mi.preferred = m->preferred;
      mi.current = wo->current_mode == m ||
                   (!wo->current_mode && m->width == wo->width && m->height == wo->height &&
                    m->refresh == wo->refresh);
      info.modes.push_back(mi);
    }
    result.push_back(std::move(info));
  }
  return result;
}

void Server::reconfigure_layer_surfaces(wlr_output* wlr_out) {
  wlr_box full_area{};
  wlr_output_layout_get_box(output_layout_, wlr_out, &full_area);
  for (const std::unique_ptr<LayerSurface>& ls : layer_surfaces) {
    if (ls->layer_surface->output != wlr_out || !ls->layer_surface->initialized) {
      continue;
    }
    wlr_box usable_area = full_area;
    wlr_scene_layer_surface_v1_configure(ls->scene_layer_surface, &full_area, &usable_area);
  }
}

void Server::revert_output_mode(Output* output) {
  if (!output->has_fallback) {
    return;
  }
  wlr_output* wo = output->wlr_output_ptr;
  OutputSetting previous;
  previous.width = output->fallback_width;
  previous.height = output->fallback_height;
  previous.refresh_mhz = output->fallback_refresh_mhz;
  output->has_fallback = false;
  output->commit_failures = 0;
  wlr_log(WLR_ERROR, "fleetwm: %s could not present the new mode; reverting to %dx%d",
          wo->name ? wo->name : "output", previous.width, previous.height);
  if (wlr_output_mode* mode = pick_mode(wo, &previous)) {
    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    wlr_output_state_set_mode(&state, mode);
    wlr_output_commit_state(wo, &state);
    wlr_output_state_finish(&state);
  }
  if (wo->name) {
    OutputSetting& saved = output_settings_[wo->name];
    saved.width = previous.width;
    saved.height = previous.height;
    saved.refresh_mhz = previous.refresh_mhz;
    try {
      save_output_settings(output_settings_);
    } catch (const std::exception&) {
    }
  }
  for (const std::unique_ptr<Output>& o : outputs) {
    reconfigure_layer_surfaces(o->wlr_output_ptr);
    o->update_usable_area();
  }
  if (ipc_server) {
    ipc_server->broadcast_outputs_changed();
  }
}

bool Server::apply_output_setting(const std::string& name, const OutputSetting& setting,
                                  std::string* error) {
  Output* target = nullptr;
  for (const std::unique_ptr<Output>& output : outputs) {
    if (output->wlr_output_ptr->name && name == output->wlr_output_ptr->name) {
      target = output.get();
      break;
    }
  }
  if (!target) {
    if (error) {
      *error = "unknown output " + name;
    }
    return false;
  }
  wlr_output* wo = target->wlr_output_ptr;

  if (setting.width > 0 && setting.height > 0) {
    wlr_output_mode* mode = pick_mode(wo, &setting);
    const bool exact = mode && mode->width == setting.width && mode->height == setting.height;
    if (!exact) {
      if (error) {
        *error = "mode " + std::to_string(setting.width) + "x" + std::to_string(setting.height) +
                 " is not supported by " + name;
      }
      return false;
    }
    if (wo->current_mode != mode) {
      if (!target->has_fallback) {
        target->has_fallback = true;
        target->fallback_width = wo->width;
        target->fallback_height = wo->height;
        target->fallback_refresh_mhz = wo->refresh;
      }
      target->commit_failures = 0;
      target->confirm_frames = 0;
      wlr_output_state state;
      wlr_output_state_init(&state);
      wlr_output_state_set_enabled(&state, true);
      wlr_output_state_set_mode(&state, mode);
      const bool ok = wlr_output_test_state(wo, &state) && wlr_output_commit_state(wo, &state);
      wlr_output_state_finish(&state);
      if (!ok) {
        if (error) {
          *error = "the monitor rejected that mode";
        }
        return false;
      }
    }
  }
  if (setting.has_pos) {
    wlr_output_layout_add(output_layout_, wo, setting.x, setting.y);
  }

  // Everything that depends on the output's size or place: layer surfaces
  // (the bar and wallpaper are anchored to the output edges and need a new
  // configure), exclusive zones, tiled windows.
  for (const std::unique_ptr<Output>& output : outputs) {
    reconfigure_layer_surfaces(output->wlr_output_ptr);
    output->update_usable_area();
  }

  OutputSetting merged = output_settings_[name];
  if (setting.width > 0 && setting.height > 0) {
    merged.width = setting.width;
    merged.height = setting.height;
    merged.refresh_mhz = wo->refresh;
  }
  if (setting.has_pos) {
    merged.has_pos = true;
    merged.x = setting.x;
    merged.y = setting.y;
  }
  output_settings_[name] = merged;
  try {
    save_output_settings(output_settings_);
  } catch (const std::exception& e) {
    wlr_log(WLR_ERROR, "fleetwm: could not save outputs.toml: %s", e.what());
  }
  if (ipc_server) {
    ipc_server->broadcast_outputs_changed();
  }
  return true;
}

bool Server::init() {
  output_settings_ = load_output_settings();
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

  // wlr_renderer_autocreate() only tries GLES2/Vulkan -- it never falls
  // back to pixman itself. On hardware whose GPU has no working GBM/EGL
  // driver at all (e.g. old Mali Midgard boards like the ODROID-XU4,
  // which have no upstream Mesa driver and fail eglInitialize outright
  // rather than just lacking an extension), that leaves the compositor
  // with no usable renderer and no way to start. Falling back to the
  // pixman software renderer here -- architecture-agnostic, no backend
  // handle needed -- means any such host still gets a working (if
  // unaccelerated) desktop instead of failing to start.
  // Without a GPU render node (VMs, headless boxes) the GLES2 renderer can only
  // run on Mesa's llvmpipe software rasterizer, which maps libLLVM and the
  // gallium driver (about 65 MB resident) and is no faster than pixman for a
  // tiling desktop. Use pixman directly there. An explicit WLR_RENDERER in the
  // environment always wins, and any machine with a render node keeps GLES2.
  if (std::getenv("WLR_RENDERER") == nullptr && !has_render_node()) {
    wlr_log(WLR_INFO, "fleetwm: no GPU render node, using the pixman renderer");
    renderer_ = wlr_pixman_renderer_create();
  }
  if (!renderer_) {
    renderer_ = wlr_renderer_autocreate(backend_);
  }
  if (!renderer_) {
    wlr_log(WLR_ERROR,
            "hardware renderer unavailable, falling back to pixman software renderer");
    renderer_ = wlr_pixman_renderer_create();
  }
  if (!renderer_) {
    return false;
  }
  // What wlr_renderer_init_wl_display() does (shm, drm, linux-dmabuf), spelled out so the dmabuf object
  // is kept: the scene needs it to tell each client which buffer formats the display hardware can scan
  // out directly, and without that a fullscreen video or game is always composited instead of shown
  // straight from its own buffer.
  wlr_renderer_init_wl_shm(renderer_, display_);
  if (wlr_renderer_get_texture_formats(renderer_, WLR_BUFFER_CAP_DMABUF) != nullptr) {
    wlr_drm_create(display_, renderer_);
    linux_dmabuf_ = wlr_linux_dmabuf_v1_create_with_renderer(display_, 4, renderer_);
  }

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
  if (linux_dmabuf_) wlr_scene_set_linux_dmabuf_v1(scene_, linux_dmabuf_);
  // Let clients scale and crop in the compositor (video players, toolkits) and hand over solid colours
  // without allocating a buffer: both are cheaper than a client resizing its own pixels.
  wlr_viewporter_create(display_);
  wlr_single_pixel_buffer_manager_v1_create(display_);

  // Always-enabled z-order layers, bottom to top -- see server.hpp for the
  // full rationale. Creation order alone establishes correct paint order
  // (wlr_scene_tree_create appends to its parent's child list).
  layer_background_ = wlr_scene_tree_create(&scene_->tree);
  layer_bottom_ = wlr_scene_tree_create(&scene_->tree);
  layer_toplevels_ = wlr_scene_tree_create(&scene_->tree);
  layer_pinned_ = wlr_scene_tree_create(&scene_->tree);
  layer_topmost_ = wlr_scene_tree_create(&scene_->tree);
  layer_top_ = wlr_scene_tree_create(&scene_->tree);
  layer_fullscreen_ = wlr_scene_tree_create(&scene_->tree);
  layer_overlay_ = wlr_scene_tree_create(&scene_->tree);
  layer_debug_ = wlr_scene_tree_create(&scene_->tree);
  wlr_scene_node_set_enabled(&layer_debug_->node, false);

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
  tearing_manager_ = wlr_tearing_control_manager_v1_create(display_, 1);

  cursor_ = wlr_cursor_create();
  wlr_cursor_attach_output_layout(cursor_, output_layout_);
  // Pick a cursor theme, and export it so every app we launch finds the same one.
  // With none installed at all, draw our own arrow instead of an invisible pointer.
  const std::string cursor_theme = pick_cursor_theme();
  if (!cursor_theme.empty()) setenv("XCURSOR_THEME", cursor_theme.c_str(), 0);
  setenv("XCURSOR_SIZE", "24", 0);
  cursor_mgr_ = wlr_xcursor_manager_create(cursor_theme.empty() ? nullptr : cursor_theme.c_str(), 24);
  if (cursor_theme.empty() || !cursor_theme_has_left_ptr(cursor_theme)) {
    wlr_log(WLR_INFO, "fleetwm: no cursor theme installed, using the built-in pointer "
                      "(install dmz-cursor-theme or adwaita-icon-theme for a proper one)");
    fallback_cursor_ = create_fallback_cursor(&fallback_hotspot_x_, &fallback_hotspot_y_);
  }

  // wp_cursor_shape_v1: clients (foot, GTK, Qt) ask for "text" or "pointer" and we draw it.
  cursor_shape_manager_ = wlr_cursor_shape_manager_v1_create(display_, 1);
  request_set_shape_.notify = [](wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, request_set_shape_);
    auto* event = static_cast<wlr_cursor_shape_manager_v1_request_set_shape_event*>(data);
    if (event->device_type != WLR_CURSOR_SHAPE_MANAGER_V1_DEVICE_TYPE_POINTER) return;
    server->apply_cursor_shape(event->seat_client, wlr_cursor_shape_v1_name(event->shape));
  };
  wl_signal_add(&cursor_shape_manager_->events.request_set_shape, &request_set_shape_);

  init_idle();

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
  // XWayland starts on demand, when the first X11 program connects (DISPLAY is set below).
  // The windows themselves are handled in xwayland.cpp.
  xwayland_ = wlr_xwayland_create(display_, compositor_, true);
  if (xwayland_) {
    // X11 programs become windows like any other (see xwayland.cpp), and share the clipboard.
    new_xwayland_surface_.notify = server_new_xwayland_surface;
    wl_signal_add(&xwayland_->events.new_surface, &new_xwayland_surface_);
    wlr_xwayland_set_seat(xwayland_, seat_);
  }
#endif

  const char* socket = wl_display_add_socket_auto(display_);
  if (!socket) {
    return false;
  }
  setenv("WAYLAND_DISPLAY", socket, true);
  // Portals and other D-Bus-activated services start outside this process; hand them the
  // session variables they need (best effort, it is fine if the tool is missing).
  setenv("XDG_CURRENT_DESKTOP", "fleetwm", false);
  if (fork() == 0) {
    execlp("dbus-update-activation-environment", "dbus-update-activation-environment", "--systemd",
           "WAYLAND_DISPLAY", "XDG_CURRENT_DESKTOP", "XDG_SESSION_DESKTOP", "QT_QPA_PLATFORMTHEME",
           static_cast<char*>(nullptr));
    _exit(127);
  }

  if (!wlr_backend_start(backend_)) {
    return false;
  }

  ipc_server = std::make_unique<IpcServer>(this);
  if (!ipc_server->listen()) {
    wlr_log(WLR_ERROR, "failed to start IPC socket; workspace switching via bar will not work");
  }

  start_signal_handlers();

  theme_config_ = load_theme_config();
  refresh_border_colors();
  titlebar_reload_palette(theme_config_);
  update_app_appearance();
  default_apps_config_ = load_default_apps_config();
  keyboard_config_ = load_keyboard_config();
  mouse_config_ = load_mouse_config();
  reload_keybinds_config();

  // Settings' Performance tab "Show performance overlay on startup" --
  // still the same debug_overlay_enabled_/layer_debug_ pair
  // toggle_debug_overlay() flips on Alt+Shift+I, just pre-set here
  // instead of always starting off.
  if (theme_config_.show_debug_overlay_on_startup) {
    debug_overlay_enabled_ = true;
    wlr_scene_node_set_enabled(&layer_debug_->node, true);
  }
  if (!start_theme_watch()) {
    wlr_log(WLR_ERROR,
            "failed to start theme.toml inotify watch; live theme reload will not work");
  }

#if FLEETWM_XWAYLAND
  if (xwayland_) {
    setenv("DISPLAY", xwayland_->display_name, true);
  }
#endif

  // Fleetwm's default foot config lives in <sysconf>/xdg/foot/foot.ini; putting that folder first in
  // XDG_CONFIG_DIRS makes every terminal started from this session use it unless the user has their
  // own ~/.config/foot/foot.ini (foot reads the user's file first).
  {
    const char* current = std::getenv("XDG_CONFIG_DIRS");
    const std::string dirs = std::string(FLEETWM_SYSCONF_DIR) + "/xdg:" + (current && *current ? current : "/etc/xdg");
    setenv("XDG_CONFIG_DIRS", dirs.c_str(), 1);
  }

  spawn_autostart("fleetwm-bar", FLEETWM_BINDIR "/fleetwm-bar");
  spawn_autostart("fleetwm-wallpaper", FLEETWM_BINDIR "/fleetwm-wallpaper");
  spawn_autostart("fleetwm-lockapplet", FLEETWM_BINDIR "/fleetwm-lockapplet");

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
    schedule_windows_broadcast();
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
#if FLEETWM_XWAYLAND
    if (wlr_xwayland_surface* prev_x = wlr_xwayland_surface_try_from_wlr_surface(prev_surface)) {
      wlr_xwayland_surface_activate(prev_x, false);
    }
#endif
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
  // Re-assert always-on-top (fleetwm-settings, and now any GTK dialog --
  // see the is_dialog block in xdg_toplevel_map) above whatever was just
  // raised, if it shares this view's workspace -- see the comment on
  // View::always_on_top (view.hpp) and raise_always_on_top_views() above.
  //
  // Real bug found live: with two always-on-top views in play (Settings
  // itself, and a color-picker dialog it just spawned), this loop iterates
  // Workspace::views() -- newest-first (add_view() push_fronts) -- and
  // raises each always_on_top view in turn, so the OLDER one (Settings)
  // ends up raised LAST and wins the top spot back from the dialog that
  // was just explicitly raised above on the previous line, even though
  // the dialog is the one that's actually focused. Re-raising `view`
  // itself again after the generic pass makes whichever view actually has
  // focus win regardless of always-on-top insertion order, instead of
  // silently depending on it.
  raise_always_on_top_views(view->workspace);
  wlr_scene_node_raise_to_top(&view->container_tree->node);

  view->set_activated(true);
  view->focused = true;
  view->resize_border();

  // Always send the enter, even when the seat has no keyboard yet (hot-plugged
  // or ephemeral virtual keyboards, VMs driven only through wlr-virtual-
  // keyboard): wlroots accepts null keycodes/modifiers, and skipping it left
  // the focused client without keyboard focus until the next focus change.
  wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat_);
  wlr_seat_keyboard_notify_enter(seat_, surface, keyboard ? keyboard->keycodes : nullptr,
                                  keyboard ? keyboard->num_keycodes : 0,
                                  keyboard ? &keyboard->modifiers : nullptr);

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
    if (view->is_window()) {
      if (view->window_title()) {
        title = view->window_title();
      } else if (view->window_app_id()) {
        title = view->window_app_id();
      }
    }
    ipc_server->broadcast_focused_title(title);
  }
  schedule_windows_broadcast();
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

int Server::cursor_scale() const {
  int scale = 1;
  for (const std::unique_ptr<Output>& output : outputs)
    scale = std::max(scale, static_cast<int>(std::ceil(output->wlr_output_ptr->scale)));
  return std::min(scale, 4);
}

void Server::refresh_cursor() {
  // The look (glass or flat) or the screen scale changed: drop the kept pictures and show the current shape again.
  cursor_pictures_clear();
  cursor_name_storage_ = cursor_name_ ? cursor_name_ : "left_ptr";
  cursor_name_ = nullptr;
  set_cursor_name(cursor_name_storage_.c_str());
}

void Server::set_cursor_name(const char* name) {
  if (cursor_name_ && std::strcmp(cursor_name_, name) == 0) return;
  cursor_name_ = name;  // callers pass string literals
  // The Windows 7 style pointer (glass: Aero, otherwise the flat version); names it does not draw go to the theme.
  kit::CursorShape shape;
  if (kit::cursor_shape_for_name(name, &shape)) {
    const int scale = cursor_scale();
    if (wlr_buffer* picture = cursor_picture(shape, theme_config_.glass, scale)) {
      const kit::CursorHotspot hot = kit::cursor_hotspot(shape);
      wlr_cursor_set_buffer(cursor_, picture, hot.x, hot.y, static_cast<float>(scale));  // the hotspot is in logical pixels
      return;
    }
  }
  if (fallback_cursor_) {
    wlr_cursor_set_buffer(cursor_, fallback_cursor_, fallback_hotspot_x_, fallback_hotspot_y_, 1.0f);
    return;
  }
  wlr_cursor_set_xcursor(cursor_, cursor_mgr_, name);
}

void Server::apply_cursor_shape(wlr_seat_client* client, const char* name) {
  if (seat_->pointer_state.focused_client != client) return;  // only the client under the pointer
  cursor_name_ = nullptr;  // force a reload even if the name matches an earlier one
  static std::string current;  // set_cursor_name keeps the pointer, so it must outlive this call
  current = name;
  set_cursor_name(current.c_str());
}

void Server::set_default_cursor_image() { set_cursor_name("left_ptr"); }

// ---- Desktop layout: interactive move / resize --------------------------

namespace {
constexpr int kMinContentW = 160;
constexpr int kMinContentH = 80;
constexpr uint32_t kDoubleClickMs = 400;
}  // namespace

void Server::begin_move(View* view) {
  if (!view || !view->output || grab_active()) return;
  grab_mode_ = GrabMode::Move;
  grab_view_ = view;
  grab_cursor_x_ = cursor_->x;
  grab_cursor_y_ = cursor_->y;
  grab_box_ = {view->container_tree->node.x, view->container_tree->node.y, 0, 0};
  grab_unmaximize_pending_ = view->maximized || view->snap_zone != geom::SnapZone::None;
  view->has_placed = false;  // a window the user moves is no longer in its tiled slot
  wlr_seat_pointer_clear_focus(seat_);
}

void Server::begin_resize(View* view, uint32_t edges) {
  if (!view || !view->output || !view->is_window() || edges == 0 || grab_active() ||
      view->maximized) {
    return;
  }
  const wlr_box geo = view->content_geometry();
  view->snap_zone = geom::SnapZone::None;  // a resized window is no longer a half/quarter
  view->has_placed = false;
  grab_mode_ = GrabMode::Resize;
  grab_view_ = view;
  grab_edges_ = edges;
  grab_cursor_x_ = cursor_->x;
  grab_cursor_y_ = cursor_->y;
  grab_box_ = {view->container_tree->node.x, view->container_tree->node.y, geo.width, geo.height};
  wlr_seat_pointer_clear_focus(seat_);
}

void Server::update_grab() {
  View* view = grab_view_;
  if (!view) {
    end_grab();
    return;
  }
  const double dx = cursor_->x - grab_cursor_x_, dy = cursor_->y - grab_cursor_y_;

  if (grab_mode_ == GrabMode::Move) {
    if (grab_unmaximize_pending_) {
      if (std::abs(dx) + std::abs(dy) < 4) return;
      // Dragging a maximized window restores it, keeping the pointer at the
      // same relative spot on the titlebar.
      const wlr_box area = view->output ? view->output->usable_area : wlr_box{};
      const double ratio = area.width > 0 ? (grab_cursor_x_ - area.x) / area.width : 0.5;
      view->restore_from_snap();
      const int outer_w = view->restore_box.width + 2 * view->border_thickness();
      grab_box_.x = static_cast<int>(grab_cursor_x_ - ratio * outer_w);
      grab_box_.y = static_cast<int>(grab_cursor_y_ - view->titlebar_height() / 2);
      grab_unmaximize_pending_ = false;
    }
    int x = static_cast<int>(grab_box_.x + dx), y = static_cast<int>(grab_box_.y + dy);
    if (view->output) {
      y = std::max(y, view->output->usable_area.y);  // keep the titlebar below the bar
    }
    wlr_scene_node_set_position(&view->container_tree->node, x, y);
    view->sync_x11_position();
    update_snap_preview(view);
    return;
  }

  if (grab_mode_ == GrabMode::Resize) {
    const geom::Box nb = geom::resized_box({grab_box_.x, grab_box_.y, grab_box_.width, grab_box_.height},
                                           grab_edges_, dx, dy, kMinContentW, kMinContentH);
    const int x = nb.x, y = nb.y, w = nb.w, h = nb.h;
    wlr_scene_node_set_position(&view->container_tree->node, x, y);
    if (w != view->last_requested_content_w || h != view->last_requested_content_h) {
      view->request_size(w, h);
      view->last_requested_content_w = w;
      view->last_requested_content_h = h;
    }
  }
}

void Server::update_snap_preview(View* view) {
  geom::SnapZone zone = geom::SnapZone::None;
  Output* out = nullptr;
  wlr_box full{};
  if (wlr_output* wo = wlr_output_layout_output_at(output_layout_, cursor_->x, cursor_->y)) {
    out = output_for(wo);
    wlr_output_layout_get_box(output_layout_, wo, &full);
  }
  // Windows only live on their own output, so snapping elsewhere is ignored.
  if (out && out == view->output && !view->fullscreen) {
    zone = geom::snap_zone_at(cursor_->x, cursor_->y, {full.x, full.y, full.width, full.height});
  }
  if (zone == snap_pending_) return;
  snap_pending_ = zone;
  if (zone == geom::SnapZone::None) {
    hide_snap_preview();
    return;
  }
  const wlr_box a = out->usable_area;
  const geom::Box box = geom::snap_box(zone, {a.x, a.y, a.width, a.height});
  float color[4] = {0.5f, 0.6f, 1.0f, 0.2f};
  if (parse_hex_color(theme_config_.accent.hex, color)) color[3] = 0.2f;
  if (!snap_preview_) {
    snap_preview_ = wlr_scene_rect_create(layer_pinned_, box.w, box.h, color);
  }
  wlr_scene_rect_set_size(snap_preview_, box.w, box.h);
  wlr_scene_rect_set_color(snap_preview_, color);
  wlr_scene_node_set_position(&snap_preview_->node, box.x, box.y);
  wlr_scene_node_set_enabled(&snap_preview_->node, true);
}

void Server::hide_snap_preview() {
  if (snap_preview_) wlr_scene_node_set_enabled(&snap_preview_->node, false);
}

void Server::end_grab() {
  if (grab_mode_ == GrabMode::Move && grab_view_ && snap_pending_ != geom::SnapZone::None) {
    const geom::SnapZone zone = snap_pending_;
    View* view = grab_view_;
    snap_pending_ = geom::SnapZone::None;
    hide_snap_preview();
    view->snap_to(zone);
  }
  snap_pending_ = geom::SnapZone::None;
  hide_snap_preview();
  grab_mode_ = GrabMode::None;
  grab_view_ = nullptr;
  grab_edges_ = 0;
  grab_unmaximize_pending_ = false;
  set_default_cursor_image();
}

void Server::forget_view(View* view) {
  if (grab_view_ == view) end_grab();
  if (hover_view_ == view) hover_view_ = nullptr;
  if (last_click_view_ == view) last_click_view_ = nullptr;
}

void Server::set_hover_view(View* view) {
  if (hover_view_ && hover_view_ != view) {
    hover_view_->set_hover_button(-1);
  }
  hover_view_ = view;
}

void Server::toggle_maximize(View* view) {
  if (view && !view->fullscreen) {
    view->set_maximized(!view->maximized);
  }
}

bool Server::is_double_click(View* view, uint32_t time_msec) {
  const bool dbl = last_click_view_ == view && time_msec - last_click_time_ <= kDoubleClickMs;
  last_click_view_ = dbl ? nullptr : view;
  last_click_time_ = time_msec;
  return dbl;
}

// ---- window list for taskbar clients --------------------------------------

void Server::minimize_view(View* view) {
  if (view) view->set_minimized(true);
}

void Server::focus_next_after(View* gone) {
  for (const std::unique_ptr<View>& candidate : views) {
    if (candidate.get() != gone && candidate->workspace && !candidate->minimized &&
        (candidate->pinned || candidate->container_tree->node.enabled)) {
      focus_view(candidate.get());
      return;
    }
  }
  focus_view(nullptr);
}

View* Server::view_by_id(uint32_t id) const {
  if (id == 0) return nullptr;
  for (const std::unique_ptr<View>& view : views) {
    if (view->id == id && view->workspace) return view.get();
  }
  return nullptr;
}

// A taskbar can list a window that lives on another workspace; show that
// workspace first so activating the window actually reveals it.
static void reveal_workspace(Server* server, View* view) {
  if (view->output && view->workspace && !view->pinned &&
      view->workspace != &view->output->active_workspace()) {
    const int index = view->workspace->index();
    view->output->switch_workspace(index);
    if (server->ipc_server) server->ipc_server->broadcast_workspace_changed(index);
  }
}

void Server::activate_view(View* view) {
  if (!view) return;
  reveal_workspace(this, view);
  if (view->minimized) {
    view->set_minimized(false);
  } else {
    focus_view(view);
  }
}

void Server::toggle_view_from_taskbar(View* view) {
  if (!view) return;
  reveal_workspace(this, view);
  const bool has_focus = seat_->keyboard_state.focused_surface == view->surface();
  if (view->minimized) {
    view->set_minimized(false);
  } else if (has_focus) {
    view->set_minimized(true);
  } else {
    focus_view(view);
  }
}

std::vector<WindowEntry> Server::window_snapshot() const {
  std::vector<WindowEntry> out;
  wlr_surface* focused = seat_->keyboard_state.focused_surface;
  for (const std::unique_ptr<View>& view : views) {
    // Only mapped, top-level windows; dialogs belong to their parent's button.
    if (!view->workspace || !view->is_window() || view->is_child_window()) {
      continue;
    }
    WindowEntry entry;
    entry.id = view->id;
    entry.focused = focused && view->surface() == focused;
    entry.minimized = view->minimized;
    entry.pinned = view->pinned;
    entry.workspace = view->workspace ? view->workspace->index() : 0;
    if (view->window_app_id()) entry.app_id = view->window_app_id();
    if (view->window_title()) entry.title = view->window_title();
    out.push_back(std::move(entry));
  }
  std::sort(out.begin(), out.end(),
            [](const WindowEntry& a, const WindowEntry& b) { return a.id < b.id; });
  return out;
}

namespace {
void windows_idle_cb(void* data) {
  auto* server = static_cast<Server*>(data);
  server->broadcast_windows_now();
}
}  // namespace

void Server::schedule_windows_broadcast() {
  if (windows_idle_ || !ipc_server || !display_) return;
  windows_idle_ = wl_event_loop_add_idle(wl_display_get_event_loop(display_), windows_idle_cb, this);
}

void Server::broadcast_windows_now() {
  windows_idle_ = nullptr;
  if (ipc_server) {
    ipc_server->broadcast_windows(format_window_list(window_snapshot()));
  }
}

void Server::toggle_debug_overlay() {
  debug_overlay_enabled_ = !debug_overlay_enabled_;
  wlr_scene_node_set_enabled(&layer_debug_->node, debug_overlay_enabled_);
  // Turning it on from an idle desktop would show nothing until something else drew a frame.
  for (const std::unique_ptr<Output>& output : outputs) wlr_output_schedule_frame(output->wlr_output_ptr);
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

// ---- keyboard layouts ----

void Server::unregister_keyboard(Keyboard* kb) {
  keyboards_.erase(std::remove(keyboards_.begin(), keyboards_.end(), kb), keyboards_.end());
}

std::string Server::layouts_line() const {
  std::string line = "LAYOUTS " + std::to_string(layout_index_);
  for (const KeyboardLayout& l : keyboard_config_.layouts) line += " " + l.layout + ":" + l.variant;
  return line;
}

void Server::apply_keyboard_config(Keyboard* kb) {
  if (kb->is_virtual) return;
  wlr_keyboard* wk = kb->wlr_keyboard_ptr;
  const XkbNames names = xkb_names_for(keyboard_config_);
  xkb_rule_names rules{};
  rules.layout = names.layout.c_str();
  rules.variant = names.variant.c_str();
  rules.model = names.model.empty() ? nullptr : names.model.c_str();
  rules.options = names.options.empty() ? nullptr : names.options.c_str();
  xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  xkb_keymap* keymap = xkb_keymap_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (!keymap) {  // a name xkb does not know: keep the keyboard usable with the default map
    wlr_log(WLR_ERROR, "fleetwm: keyboard.toml: xkb rejected layout '%s' variant '%s'; using the default",
            names.layout.c_str(), names.variant.c_str());
    keymap = xkb_keymap_new_from_names(context, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
  }
  if (keymap) {
    wlr_keyboard_set_keymap(wk, keymap);
    xkb_keymap_unref(keymap);
  }
  xkb_context_unref(context);
  wlr_keyboard_set_repeat_info(wk, keyboard_config_.repeat_rate, keyboard_config_.repeat_delay);
  const int n = static_cast<int>(xkb_keymap_num_layouts(wk->keymap));
  if (n > 0) {
    wlr_keyboard_notify_modifiers(wk, wk->modifiers.depressed, wk->modifiers.latched, wk->modifiers.locked,
                                  static_cast<uint32_t>(std::clamp(layout_index_, 0, n - 1)));
  }
}

void Server::apply_mouse_config(wlr_input_device* device) {
  if (std::find(mouse_devices_.begin(), mouse_devices_.end(), device) == mouse_devices_.end()) mouse_devices_.push_back(device);
  if (!wlr_input_device_is_libinput(device)) return;  // nested, virtual and headless pointers have no acceleration to set
  libinput_device* li = wlr_libinput_get_device_handle(device);
  if (!li || !libinput_device_config_accel_is_available(li)) return;
  libinput_device_config_accel_set_speed(li, libinput_speed_for_notch(mouse_config_.speed));
  const uint32_t profiles = libinput_device_config_accel_get_profiles(li);
  const libinput_config_accel_profile want =
      wants_adaptive_profile(mouse_config_) ? LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE : LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
  if (profiles & want) libinput_device_config_accel_set_profile(li, want);
}

void Server::reload_mouse_config() {
  mouse_config_ = load_mouse_config();
  for (wlr_input_device* device : mouse_devices_) apply_mouse_config(device);
}

void Server::forget_mouse_device(wlr_input_device* device) {
  mouse_devices_.erase(std::remove(mouse_devices_.begin(), mouse_devices_.end(), device), mouse_devices_.end());
}

void Server::reload_keyboard_config() {
  keyboard_config_ = load_keyboard_config();
  layout_index_ = std::clamp(layout_index_, 0, static_cast<int>(keyboard_config_.layouts.size()) - 1);
  for (Keyboard* kb : keyboards_) apply_keyboard_config(kb);
  if (ipc_server) ipc_server->broadcast_line(layouts_line());
}

void Server::set_layout(int index) {
  const int n = static_cast<int>(keyboard_config_.layouts.size());
  if (n <= 0) return;
  layout_index_ = ((index % n) + n) % n;
  for (Keyboard* kb : keyboards_) {
    if (kb->is_virtual) continue;
    wlr_keyboard* wk = kb->wlr_keyboard_ptr;
    wlr_keyboard_notify_modifiers(wk, wk->modifiers.depressed, wk->modifiers.latched, wk->modifiers.locked,
                                  static_cast<uint32_t>(layout_index_));
  }
  if (ipc_server) ipc_server->broadcast_line(layouts_line());
}

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
  resolved_keybinds_.debug_overlay =
      resolve_keybind(keybinds_config_.debug_overlay, defaults.debug_overlay, "debug_overlay");
  auto resolve_combo = [&](const std::string& text, const Server::ResolvedKeybinds::Combo& fallback,
                           const char* field) {
    const KeyCombo combo = parse_key_combo(text);
    const xkb_keysym_t sym =
        combo.valid ? xkb_keysym_from_name(combo.key.c_str(), XKB_KEYSYM_NO_FLAGS) : XKB_KEY_NoSymbol;
    if (!combo.valid || sym == XKB_KEY_NoSymbol) {
      wlr_log(WLR_ERROR, "fleetwm: keybinds.toml: unrecognized combo '%s' for '%s', keeping the default",
              text.c_str(), field);
      return fallback;
    }
    return Server::ResolvedKeybinds::Combo{combo.mods, sym};
  };
  resolved_keybinds_.shortcuts_help = resolve_combo(keybinds_config_.shortcuts_help, defaults.shortcuts_help, "shortcuts_help");
  resolved_keybinds_.keyboard_next_layout =
      resolve_combo(keybinds_config_.keyboard_next_layout, defaults.keyboard_next_layout, "keyboard_next_layout");
  resolved_keybinds_.keyboard_prev_layout =
      resolve_combo(keybinds_config_.keyboard_prev_layout, defaults.keyboard_prev_layout, "keyboard_prev_layout");
  resolved_keybinds_.tiling_mod = keybinds_config_.tiling_modifier == "super" ? kModLogo : kModAlt;
  resolved_keybinds_.desktop_terminal = resolve_combo(keybinds_config_.desktop_terminal, defaults.desktop_terminal, "desktop_terminal");
  resolved_keybinds_.desktop_browser = resolve_combo(keybinds_config_.desktop_browser, defaults.desktop_browser, "desktop_browser");
  resolved_keybinds_.desktop_file_manager =
      resolve_combo(keybinds_config_.desktop_file_manager, defaults.desktop_file_manager, "desktop_file_manager");
  resolved_keybinds_.desktop_text_editor =
      resolve_combo(keybinds_config_.desktop_text_editor, defaults.desktop_text_editor, "desktop_text_editor");
  resolved_keybinds_.desktop_debug_overlay =
      resolve_combo(keybinds_config_.desktop_debug_overlay, defaults.desktop_debug_overlay, "desktop_debug_overlay");
  resolved_keybinds_.desktop_snap_left = resolve_combo(keybinds_config_.desktop_snap_left, defaults.desktop_snap_left, "desktop_snap_left");
  resolved_keybinds_.desktop_snap_right = resolve_combo(keybinds_config_.desktop_snap_right, defaults.desktop_snap_right, "desktop_snap_right");
  resolved_keybinds_.desktop_snap_up = resolve_combo(keybinds_config_.desktop_snap_up, defaults.desktop_snap_up, "desktop_snap_up");
  resolved_keybinds_.desktop_snap_down = resolve_combo(keybinds_config_.desktop_snap_down, defaults.desktop_snap_down, "desktop_snap_down");
  resolved_keybinds_.desktop_close_window = resolve_combo(keybinds_config_.desktop_close_window, defaults.desktop_close_window, "desktop_close_window");
  resolved_keybinds_.desktop_toggle_maximize =
      resolve_combo(keybinds_config_.desktop_toggle_maximize, defaults.desktop_toggle_maximize, "desktop_toggle_maximize");
  resolved_keybinds_.desktop_show_desktop = resolve_combo(keybinds_config_.desktop_show_desktop, defaults.desktop_show_desktop, "desktop_show_desktop");
  resolved_keybinds_.desktop_minimize_all = resolve_combo(keybinds_config_.desktop_minimize_all, defaults.desktop_minimize_all, "desktop_minimize_all");
  resolved_keybinds_.desktop_restore_all = resolve_combo(keybinds_config_.desktop_restore_all, defaults.desktop_restore_all, "desktop_restore_all");
  resolved_keybinds_.cycle_windows = resolve_combo(keybinds_config_.cycle_windows, defaults.cycle_windows, "cycle_windows");
  resolved_keybinds_.cycle_windows_reverse =
      resolve_combo(keybinds_config_.cycle_windows_reverse, defaults.cycle_windows_reverse, "cycle_windows_reverse");
  resolved_keybinds_.send_to_prev_screen =
      resolve_combo(keybinds_config_.send_to_prev_screen, defaults.send_to_prev_screen, "send_to_prev_screen");
  resolved_keybinds_.send_to_next_screen =
      resolve_combo(keybinds_config_.send_to_next_screen, defaults.send_to_next_screen, "send_to_next_screen");
  resolved_keybinds_.workspace_prev = resolve_combo(keybinds_config_.workspace_prev, defaults.workspace_prev, "workspace_prev");
  resolved_keybinds_.workspace_next = resolve_combo(keybinds_config_.workspace_next, defaults.workspace_next, "workspace_next");
  {
    const unsigned sw = modifier_mask(keybinds_config_.workspace_switch);
    const unsigned sd = modifier_mask(keybinds_config_.workspace_send);
    if (sw == 0 || sd == 0 || sw == sd) {
      wlr_log(WLR_ERROR, "fleetwm: keybinds.toml: workspace_switch/workspace_send must be two different "
                         "modifier sets; keeping super / super+shift");
    } else {
      resolved_keybinds_.workspace_switch_mods = sw;
      resolved_keybinds_.workspace_send_mods = sd;
    }
  }

  resolved_keybinds_.start_menu_syms.clear();
  for (const std::string& name : split_key_names(keybinds_config_.start_menu_key)) {
    const xkb_keysym_t sym = xkb_keysym_from_name(name.c_str(), XKB_KEYSYM_NO_FLAGS);
    if (sym == XKB_KEY_NoSymbol) {
      wlr_log(WLR_ERROR, "fleetwm: keybinds.toml: unrecognized key name '%s' for 'start_menu_key'", name.c_str());
    } else {
      resolved_keybinds_.start_menu_syms.push_back(sym);
    }
  }
  if (resolved_keybinds_.start_menu_syms.empty()) {
    resolved_keybinds_.start_menu_syms = {XKB_KEY_Super_L, XKB_KEY_Super_R};
  }
}

// GTK, Chromium and Qt apps follow the theme's dark/light setting; only touched when it changes.
void Server::update_app_appearance() {
  const bool dark = theme_is_dark(theme_config_.theme);
  if (appearance_applied_ && appearance_dark_ == dark) return;
  appearance_applied_ = true;
  appearance_dark_ = dark;
  apply_app_appearance(dark);
}

void Server::refresh_border_colors() {
  const float fallback[4] = {0.9f, 0.9f, 0.95f, 1.0f};
  auto parse = [&](const std::string& hex, float* out) {
    std::copy(fallback, fallback + 4, out);
    parse_hex_color(hex, out);
  };
  parse(theme_config_.pinned_focused_border_color, border_colors_.pinned_focused);
  parse(theme_config_.pinned_border_color, border_colors_.pinned);
  parse(theme_config_.focus_border_color, border_colors_.focus);
}

void Server::reload_theme_config() {
  const bool was_desktop = desktop_layout();
  theme_config_ = load_theme_config();
  refresh_border_colors();
  titlebar_reload_palette(theme_config_);
  refresh_cursor();  // glass on or off changes the pointer
  update_app_appearance();
  for (const std::unique_ptr<View>& view : views) {  // panels follow the layout's stacking rule
    if (!view->fleetwm_panel) continue;
    const bool want_top = !desktop_layout();
    if (view->always_on_top != want_top) {
      view->always_on_top = want_top;
      view->update_stacking_layer();
    }
  }
  if (was_desktop && !desktop_layout()) {
    end_grab();
    for (const std::unique_ptr<View>& view : views) {
      view->snap_zone = geom::SnapZone::None;  // tiling places windows itself
      view->has_placed = false;
      if (view->maximized) view->set_maximized(false);
      // Nothing in the tiling layout can bring a minimized window back.
      if (view->minimized) view->set_minimized(false);
    }
  }
  for (const std::unique_ptr<View>& view : views) {
    view->invalidate_titlebar();
    view->resize_border();
  }
  // The gap kept next to layer-shell bars depends on the layout, so the work
  // area may have changed.
  for (const std::unique_ptr<Output>& output : outputs) {
    output->update_usable_area();  // also covers a changed bar gap
  }
  // Tiling -> Desktop: keep the windows where tiling had them.
  if (!was_desktop && desktop_layout()) {
    for (const std::unique_ptr<Output>& output : outputs) {
      output->snap_tiled_windows();
    }
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
  bool got_power_event = false;
  bool got_keyboard_event = false;
  bool got_mouse_event = false;
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
      } else if (event->len > 0 && std::strcmp(event->name, "power.toml") == 0) {
        got_power_event = true;
      } else if (event->len > 0 && std::strcmp(event->name, "keyboard.toml") == 0) {
        got_keyboard_event = true;
      } else if (event->len > 0 && std::strcmp(event->name, "mouse.toml") == 0) {
        got_mouse_event = true;
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
  if (got_power_event) {
    server->reload_power_config();
  }
  if (got_keyboard_event) {
    server->reload_keyboard_config();
  }
  if (got_mouse_event) {
    server->reload_mouse_config();
  }
  return 0;
}

namespace {

int server_signal_terminate(int, void* data) {
  auto* server = static_cast<Server*>(data);
  wl_display_terminate(server->display());
  return 0;
}

// Every fork()+execlp() child this compositor spawns (terminal/launcher
// keybinds in input.cpp, autostart apps and fleetwm-locker in this file)
// had nothing reaping its exit status -- each one sat around as a
// zombie (`[foot] <defunct>`) forever once its process exited. Zombies
// themselves cost almost nothing (just a process-table slot), but
// finding this is what led to actually diagnosing the real memory
// growth: a burst of terminal spawns left the compositor's own glibc
// heap arena holding tens of MB of freed-but-never-trimmed memory
// (confirmed live via malloc_trim(0) reclaiming ~60MB from a session
// that had spawned and closed ~90 terminals) -- see start_signal_handlers's
// mallopt() call below for the actual fix for that part. This handler
// just stops the zombies from piling up: wl_event_loop_add_signal's
// SIGCHLD delivery happens on the main event loop thread (not real
// signal-handler context), so a plain waitpid() loop here is safe.
int server_signal_child(int, void* data) {
  auto* server = static_cast<Server*>(data);
  // Reap only children we own. A blanket waitpid(-1) also steals Xwayland's
  // exit status, which wlroots waits for itself ("waitpid for Xwayland fork
  // failed: No child processes") and then treats as a failed server start.
  const pid_t skip = server->xwayland_server_pid();
  std::vector<pid_t> children;
  if (std::FILE* f = std::fopen(("/proc/self/task/" + std::to_string(getpid()) + "/children").c_str(), "r")) {
    long p;
    while (std::fscanf(f, "%ld", &p) == 1) children.push_back(static_cast<pid_t>(p));
    std::fclose(f);
  }
  if (children.empty()) {
    // /proc unavailable: fall back to reaping everything.
    int status = 0;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) server->on_child_exited(pid, status);
    return 0;
  }
  for (pid_t child : children) {
    if (child == skip) continue;
    int status = 0;
    if (waitpid(child, &status, WNOHANG) == child) server->on_child_exited(child, status);
  }
  return 0;
}

}  // namespace

pid_t Server::xwayland_server_pid() const {
#if FLEETWM_XWAYLAND
  if (xwayland_ && xwayland_->server) return xwayland_->server->pid;
#endif
  return 0;
}

void Server::start_signal_handlers() {
  wl_event_loop* loop = wl_display_get_event_loop(display_);
  sigterm_source_ = wl_event_loop_add_signal(loop, SIGTERM, server_signal_terminate, this);
  sigint_source_ = wl_event_loop_add_signal(loop, SIGINT, server_signal_terminate, this);
  sigchld_source_ = wl_event_loop_add_signal(loop, SIGCHLD, server_signal_child, this);
  if (sigterm_source_ == nullptr || sigint_source_ == nullptr) {
    wlr_log(WLR_ERROR, "failed to register SIGTERM/SIGINT handlers; kill will not shut down "
                        "cleanly (clients won't be notified, atexit hooks won't run)");
  }
  if (sigchld_source_ == nullptr) {
    wlr_log(WLR_ERROR, "failed to register SIGCHLD handler; spawned child processes (terminal, "
                        "autostart apps, locker) will accumulate as zombies");
  }

  // See tune_malloc_for_low_rss()'s own doc comment (src/common/
  // malloc_tuning.hpp) for why -- confirmed live on fleetwm-dev: Pss
  // held steady across repeated terminal spawn/close cycles with this
  // set, vs. climbing from ~90MB to 150MB+ and staying there without
  // it.
  tune_malloc_for_low_rss();
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
