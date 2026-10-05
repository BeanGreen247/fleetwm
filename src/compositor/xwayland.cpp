// X11 programs (through XWayland) as ordinary windows: they are tiled, floated, snapped,
// focused and listed in the taskbar exactly like Wayland windows, and menus and tooltips
// from X11 programs show up where the program puts them.
//
// Windows the program manages itself ("override redirect": menus, tooltips, drop-downs) are
// not windows at all here -- they are just drawn at the position they ask for, above everything.

#include <algorithm>
#include <memory>

#include "config.h"
#include "output.hpp"
#include "scene_node_owner.hpp"
#include "server.hpp"
#include "view.hpp"
#include "workspace.hpp"

#if FLEETWM_XWAYLAND

namespace fleetwm {

namespace {

// wl_list_remove() crashes on a listener that was never added; every listener here starts
// zeroed, so "not linked" is a null prev pointer. Safe to call twice.
void unlink(wl_listener* l) {
  if (l->link.prev) {
    wl_list_remove(&l->link);
    l->link.prev = nullptr;
    l->link.next = nullptr;
  }
}

// ---- windows managed like any other ----------------------------------------------------

void drop_scene(View* view) {
  if (!view->x_scene) return;
  wlr_scene_node* node = &view->x_scene->buffer->node;
  unlink(&view->x_scene_destroy);
  view->x_scene = nullptr;
  wlr_scene_node_destroy(node);
}

void x11_scene_destroyed(wl_listener* l, void*) {
  View* view = wl_container_of(l, view, x_scene_destroy);
  unlink(&view->x_scene_destroy);
  view->x_scene = nullptr;  // the surface went away first
}

void x11_map(wl_listener* l, void*) {
  View* view = wl_container_of(l, view, map);
  wlr_xwayland_surface* xs = view->xwayland_surface;
  if (!xs || !xs->surface) return;
  drop_scene(view);
  view->x_scene = wlr_scene_surface_create(view->scene_tree, xs->surface);
  if (view->x_scene) {
    view->x_scene_destroy.notify = x11_scene_destroyed;
    wl_signal_add(&view->x_scene->buffer->node.events.destroy, &view->x_scene_destroy);
  }
  view_mapped(view);
}

void x11_unmap(wl_listener* l, void*) {
  View* view = wl_container_of(l, view, unmap);
  view_unmapped(view);
  drop_scene(view);
}

void x11_commit(wl_listener* l, void*) {
  View* view = wl_container_of(l, view, surface_commit);
  view->resize_border();
}

void x11_associate(wl_listener* l, void*) {
  View* view = wl_container_of(l, view, x_associate);
  wlr_surface* surface = view->xwayland_surface ? view->xwayland_surface->surface : nullptr;
  if (!surface) return;
  unlink(&view->map);
  unlink(&view->unmap);
  unlink(&view->surface_commit);
  view->map.notify = x11_map;
  wl_signal_add(&surface->events.map, &view->map);
  view->unmap.notify = x11_unmap;
  wl_signal_add(&surface->events.unmap, &view->unmap);
  view->surface_commit.notify = x11_commit;
  wl_signal_add(&surface->events.commit, &view->surface_commit);
}

void x11_dissociate(wl_listener* l, void*) {
  View* view = wl_container_of(l, view, x_dissociate);
  if (view->workspace) view_unmapped(view);
  drop_scene(view);
  unlink(&view->map);
  unlink(&view->unmap);
  unlink(&view->surface_commit);
}

void x11_destroy(wl_listener* l, void*) {
  View* view = wl_container_of(l, view, destroy);
  Server* server = view->server;
  if (view->workspace) view_unmapped(view);
  drop_scene(view);
  for (wl_listener* listener :
       {&view->map, &view->unmap, &view->surface_commit, &view->destroy, &view->request_configure, &view->request_move,
        &view->request_resize, &view->request_maximize, &view->request_fullscreen, &view->set_title,
        &view->x_set_class, &view->x_request_activate, &view->x_request_minimize, &view->x_associate,
        &view->x_dissociate}) {
    unlink(listener);
  }
  server->forget_view(view);
  wlr_scene_node_destroy(&view->container_tree->node);
  server->views.remove_if([view](const std::unique_ptr<View>& v) { return v.get() == view; });
}

void x11_request_configure(wl_listener* l, void* data) {
  View* view = wl_container_of(l, view, request_configure);
  auto* event = static_cast<wlr_xwayland_surface_configure_event*>(data);
  wlr_xwayland_surface* xs = view->xwayland_surface;
  if (!view->workspace) {
    // Not shown yet: the program may start at the size it wants.
    wlr_xwayland_surface_configure(xs, event->x, event->y, event->width, event->height);
    return;
  }
  const bool free_window = view->desktop_mode() && !view->maximized && !view->fullscreen &&
                           view->snap_zone == geom::SnapZone::None && !view->has_placed;
  if (free_window) {
    // A floating window may resize itself; it keeps its place.
    view->request_size(event->width, event->height);
    view->last_requested_content_w = event->width;
    view->last_requested_content_h = event->height;
    return;
  }
  // Tiled, snapped or maximized: the size is ours to decide; tell the program what it has.
  const wlr_box geo = view->content_geometry();
  const int w = view->last_requested_content_w > 0 ? view->last_requested_content_w : geo.width;
  const int h = view->last_requested_content_h > 0 ? view->last_requested_content_h : geo.height;
  view->request_size(w, h);
}

void x11_request_move(wl_listener* l, void*) {
  View* view = wl_container_of(l, view, request_move);
  if (view->server->desktop_layout() && !view->fullscreen) view->server->begin_move(view);
}

void x11_request_resize(wl_listener* l, void* data) {
  View* view = wl_container_of(l, view, request_resize);
  auto* event = static_cast<wlr_xwayland_resize_event*>(data);
  if (view->server->desktop_layout() && !view->fullscreen) view->server->begin_resize(view, event->edges);
}

void x11_request_maximize(wl_listener* l, void*) {
  View* view = wl_container_of(l, view, request_maximize);
  wlr_xwayland_surface* xs = view->xwayland_surface;
  if (view->server->desktop_layout() && view->output) view->set_maximized(xs->maximized_horz && xs->maximized_vert);
}

void x11_request_fullscreen(wl_listener* l, void*) {
  View* view = wl_container_of(l, view, request_fullscreen);
  view->set_fullscreen(view->xwayland_surface->fullscreen);
}

void x11_request_activate(wl_listener* l, void*) {
  View* view = wl_container_of(l, view, x_request_activate);
  if (view->workspace && view->output && !view->minimized &&
      (view->pinned || view->workspace == &view->output->active_workspace())) {
    view->server->focus_view(view);
  }
}

void x11_request_minimize(wl_listener* l, void* data) {
  View* view = wl_container_of(l, view, x_request_minimize);
  auto* event = static_cast<wlr_xwayland_minimize_event*>(data);
  view->set_minimized(event->minimize);
}

void x11_set_title(wl_listener* l, void*) {
  View* view = wl_container_of(l, view, set_title);
  view->update_titlebar();
  view->server->schedule_windows_broadcast();
}

void x11_set_class(wl_listener* l, void*) {
  View* view = wl_container_of(l, view, x_set_class);
  view->server->schedule_windows_broadcast();
}

// ---- menus, tooltips and other windows the program places itself ------------------------

struct Unmanaged {
  SceneNodeOwner owner = SceneNodeOwner::Unmanaged;  // must stay first: hit-testing reads it
  Server* server = nullptr;
  wlr_xwayland_surface* xs = nullptr;
  wlr_scene_tree* tree = nullptr;
  wl_listener associate{}, dissociate{}, map{}, unmap{}, destroy{}, request_configure{}, set_geometry{};
};

void unmanaged_place(Unmanaged* u) {
  if (u->tree) wlr_scene_node_set_position(&u->tree->node, u->xs->x, u->xs->y);
}

void unmanaged_drop_tree(Unmanaged* u) {
  if (!u->tree) return;
  wlr_scene_node_destroy(&u->tree->node);
  u->tree = nullptr;
}

void unmanaged_map(wl_listener* l, void*) {
  Unmanaged* u = wl_container_of(l, u, map);
  if (!u->xs->surface) return;
  unmanaged_drop_tree(u);
  u->tree = wlr_scene_tree_create(u->server->layer_topmost());
  u->tree->node.data = u;
  wlr_scene_surface_create(u->tree, u->xs->surface);
  unmanaged_place(u);
}

void unmanaged_unmap(wl_listener* l, void*) {
  Unmanaged* u = wl_container_of(l, u, unmap);
  unmanaged_drop_tree(u);
}

void unmanaged_associate(wl_listener* l, void*) {
  Unmanaged* u = wl_container_of(l, u, associate);
  wlr_surface* surface = u->xs->surface;
  if (!surface) return;
  unlink(&u->map);
  unlink(&u->unmap);
  u->map.notify = unmanaged_map;
  wl_signal_add(&surface->events.map, &u->map);
  u->unmap.notify = unmanaged_unmap;
  wl_signal_add(&surface->events.unmap, &u->unmap);
}

void unmanaged_dissociate(wl_listener* l, void*) {
  Unmanaged* u = wl_container_of(l, u, dissociate);
  unmanaged_drop_tree(u);
  unlink(&u->map);
  unlink(&u->unmap);
}

void unmanaged_configure(wl_listener* l, void* data) {
  Unmanaged* u = wl_container_of(l, u, request_configure);
  auto* event = static_cast<wlr_xwayland_surface_configure_event*>(data);
  wlr_xwayland_surface_configure(u->xs, event->x, event->y, event->width, event->height);
}

void unmanaged_geometry(wl_listener* l, void*) {
  Unmanaged* u = wl_container_of(l, u, set_geometry);
  unmanaged_place(u);
}

void unmanaged_destroy(wl_listener* l, void*) {
  Unmanaged* u = wl_container_of(l, u, destroy);
  unmanaged_drop_tree(u);
  for (wl_listener* listener : {&u->associate, &u->dissociate, &u->map, &u->unmap, &u->destroy,
                                &u->request_configure, &u->set_geometry}) {
    unlink(listener);
  }
  delete u;
}

void new_unmanaged(Server* server, wlr_xwayland_surface* xs) {
  auto* u = new Unmanaged;
  u->server = server;
  u->xs = xs;
  u->associate.notify = unmanaged_associate;
  wl_signal_add(&xs->events.associate, &u->associate);
  u->dissociate.notify = unmanaged_dissociate;
  wl_signal_add(&xs->events.dissociate, &u->dissociate);
  u->destroy.notify = unmanaged_destroy;
  wl_signal_add(&xs->events.destroy, &u->destroy);
  u->request_configure.notify = unmanaged_configure;
  wl_signal_add(&xs->events.request_configure, &u->request_configure);
  u->set_geometry.notify = unmanaged_geometry;
  wl_signal_add(&xs->events.set_geometry, &u->set_geometry);
  if (xs->surface) unmanaged_associate(&u->associate, nullptr);
}

}  // namespace

void server_new_xwayland_surface(wl_listener* listener, void* data) {
  Server* server = wl_container_of(listener, server, new_xwayland_surface_);
  auto* xs = static_cast<wlr_xwayland_surface*>(data);
  if (xs->override_redirect) {
    new_unmanaged(server, xs);
    return;
  }

  auto owned = std::make_unique<View>(server, View::Kind::XWayland);
  View* view = owned.get();
  view->xwayland_surface = xs;
  view->id = server->next_view_id_++;
  view->container_tree = wlr_scene_tree_create(server->layer_toplevels());
  view->scene_tree = wlr_scene_tree_create(view->container_tree);
  view->scene_tree->node.data = view;
  create_view_rects(view);

  view->destroy.notify = x11_destroy;
  wl_signal_add(&xs->events.destroy, &view->destroy);
  view->request_configure.notify = x11_request_configure;
  wl_signal_add(&xs->events.request_configure, &view->request_configure);
  view->request_move.notify = x11_request_move;
  wl_signal_add(&xs->events.request_move, &view->request_move);
  view->request_resize.notify = x11_request_resize;
  wl_signal_add(&xs->events.request_resize, &view->request_resize);
  view->request_maximize.notify = x11_request_maximize;
  wl_signal_add(&xs->events.request_maximize, &view->request_maximize);
  view->request_fullscreen.notify = x11_request_fullscreen;
  wl_signal_add(&xs->events.request_fullscreen, &view->request_fullscreen);
  view->set_title.notify = x11_set_title;
  wl_signal_add(&xs->events.set_title, &view->set_title);
  view->x_set_class.notify = x11_set_class;
  wl_signal_add(&xs->events.set_class, &view->x_set_class);
  view->x_request_activate.notify = x11_request_activate;
  wl_signal_add(&xs->events.request_activate, &view->x_request_activate);
  view->x_request_minimize.notify = x11_request_minimize;
  wl_signal_add(&xs->events.request_minimize, &view->x_request_minimize);
  view->x_associate.notify = x11_associate;
  wl_signal_add(&xs->events.associate, &view->x_associate);
  view->x_dissociate.notify = x11_dissociate;
  wl_signal_add(&xs->events.dissociate, &view->x_dissociate);

  server->views.push_front(std::move(owned));
  if (xs->surface) x11_associate(&view->x_associate, nullptr);
}

}  // namespace fleetwm

#else  // built without XWayland support

namespace fleetwm {
void server_new_xwayland_surface(wl_listener*, void*) {}
}  // namespace fleetwm

#endif
