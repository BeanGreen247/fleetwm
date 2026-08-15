#include "server.hpp"

#include <wayland-server-core.h>

extern "C" {
#include <wlr/util/log.h>
}

#include <algorithm>
#include <cstdlib>
#include <memory>

#include "input.hpp"
#include "ipc_server.hpp"
#include "layer_surface.hpp"
#include "output.hpp"
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

  server->outputs.push_back(std::move(output));
}

// -- xdg toplevels -------------------------------------------------------

static void xdg_toplevel_map(wl_listener* listener, void*) {
  View* view = wl_container_of(listener, view, map);

  if (!view->server->outputs.empty()) {
    Output* output = view->server->outputs.front().get();
    Workspace& workspace = output->active_workspace();
    workspace.add_view(view);
    view->workspace = &workspace;

    wlr_box box{};
    wlr_output_layout_get_box(view->server->output_layout(), output->wlr_output_ptr, &box);
    wlr_scene_node_set_position(&view->scene_tree->node, box.x, box.y);
  }

  view->server->focus_view(view);
}

static void xdg_toplevel_unmap(wl_listener* listener, void*) {
  View* view = wl_container_of(listener, view, unmap);
  if (view->workspace) {
    view->workspace->remove_view(view);
    view->workspace = nullptr;
  }
}

static void xdg_toplevel_destroy(wl_listener* listener, void*) {
  View* view = wl_container_of(listener, view, destroy);
  Server* server = view->server;
  server->views.remove_if(
      [view](const std::unique_ptr<View>& v) { return v.get() == view; });
}

static void xdg_toplevel_request_move(wl_listener* listener, void*) {
  (void)listener;  // Phase 1 scope: floating drag-move.
}

static void xdg_toplevel_request_resize(wl_listener* listener, void*) {
  (void)listener;  // Phase 1 scope: floating interactive resize.
}

void server_new_xdg_toplevel(wl_listener* listener, void* data) {
  Server* server = wl_container_of(listener, server, new_xdg_toplevel_);
  auto* toplevel = static_cast<wlr_xdg_toplevel*>(data);

  auto view = std::make_unique<View>(server, View::Kind::XdgToplevel);
  view->xdg_toplevel = toplevel;
  view->scene_tree = wlr_scene_xdg_surface_create(server->layer_toplevels_, toplevel->base);
  view->scene_tree->node.data = view.get();
  toplevel->base->data = view->scene_tree;

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

  server->views.push_front(std::move(view));
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

  // Configure with the output's full effective box. No exclusive-zone
  // accumulation across sibling layer surfaces yet (Phase 1+ scope, once
  // a client actually requests one, e.g. the future bar) -- an unanchored
  // surface like the launcher is centered automatically by
  // wlr_scene_layer_surface_v1's internal commit handling, so the full
  // box is already correct for that case.
  wlr_box box{};
  wlr_output_layout_get_box(server->output_layout_, layer_surface->output, &box);
  wlr_layer_surface_v1_configure(layer_surface, static_cast<uint32_t>(box.width),
                                  static_cast<uint32_t>(box.height));

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
  if (out->owner == SceneNodeOwner::View) {
    out->surface = static_cast<View*>(tree->node.data)->surface();
  } else {
    out->surface = static_cast<LayerSurface*>(tree->node.data)->surface();
  }
  return true;
}

static View* view_at(Server* server, double lx, double ly, wlr_surface** surface, double* sx,
                      double* sy) {
  SceneHit hit{};
  if (!scene_node_at(server, lx, ly, sx, sy, &hit) || hit.owner != SceneNodeOwner::View) {
    return nullptr;
  }
  *surface = hit.surface;
  return static_cast<View*>(hit.data);
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
  if (display_) {
    wl_display_destroy_clients(display_);
    wl_display_destroy(display_);
  }
}

bool Server::init() {
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
  layer_top_ = wlr_scene_tree_create(&scene_->tree);
  layer_overlay_ = wlr_scene_tree_create(&scene_->tree);

  new_output_.notify = server_new_output;
  wl_signal_add(&backend_->events.new_output, &new_output_);

  xdg_shell_ = wlr_xdg_shell_create(display_, 3);
  new_xdg_toplevel_.notify = server_new_xdg_toplevel;
  wl_signal_add(&xdg_shell_->events.new_toplevel, &new_xdg_toplevel_);

  layer_shell_ = wlr_layer_shell_v1_create(display_, 4);
  new_layer_surface_.notify = server_new_layer_surface;
  wl_signal_add(&layer_shell_->events.new_surface, &new_layer_surface_);

  cursor_ = wlr_cursor_create();
  wlr_cursor_attach_output_layout(cursor_, output_layout_);
  cursor_mgr_ = wlr_xcursor_manager_create(nullptr, 24);

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

#if FLEETWM_XWAYLAND
  if (xwayland_) {
    setenv("DISPLAY", xwayland_->display_name, true);
  }
#endif

  return true;
}

void Server::run() {
  wl_display_run(display_);
}

void Server::focus_view(View* view) {
  if (!view) {
    wlr_seat_keyboard_clear_focus(seat_);
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
  }

  auto it = std::find_if(views.begin(), views.end(),
                          [view](const std::unique_ptr<View>& v) { return v.get() == view; });
  if (it != views.end() && it != views.begin()) {
    views.splice(views.begin(), views, it);  // move to front (topmost) without destroying
  }
  wlr_scene_node_raise_to_top(&view->scene_tree->node);

  if (view->kind == View::Kind::XdgToplevel && view->xdg_toplevel) {
    wlr_xdg_toplevel_set_activated(view->xdg_toplevel, true);
  }

  wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat_);
  if (keyboard) {
    wlr_seat_keyboard_notify_enter(seat_, surface, keyboard->keycodes, keyboard->num_keycodes,
                                    &keyboard->modifiers);
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

}  // namespace fleetwm
