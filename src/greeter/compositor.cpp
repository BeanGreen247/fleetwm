#include "compositor.hpp"

#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

extern "C" {
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/util/log.h>
}

#include <cstdio>
#include <ctime>

namespace fleetwm::greeter {

namespace {

// Sole-client keyboard forwarding: unlike src/compositor/input.cpp's
// Keyboard class, there is no keybind layer here at all -- every key event
// (including Tab/Enter for form navigation) goes straight to the login
// client, the only thing that could ever be focused. Holds `compositor`
// (a public method surface, see compositor.hpp) rather than needing full
// friend access, only to keep seat capabilities in sync on add/remove.
struct Keyboard {
  GreeterCompositor* compositor;
  wlr_keyboard* wlr_keyboard_ptr;
  wl_listener modifiers{};
  wl_listener key{};
  wl_listener destroy{};
};

void keyboard_modifiers(wl_listener* listener, void*) {
  Keyboard* kb = wl_container_of(listener, kb, modifiers);
  if (kb->compositor->is_shutting_down()) {
    return;  // see GreeterCompositor::is_shutting_down()'s doc comment
  }
  wlr_seat* seat = kb->compositor->seat();
  wlr_seat_set_keyboard(seat, kb->wlr_keyboard_ptr);
  wlr_seat_keyboard_notify_modifiers(seat, &kb->wlr_keyboard_ptr->modifiers);
}

void keyboard_key(wl_listener* listener, void* data) {
  Keyboard* kb = wl_container_of(listener, kb, key);
  if (kb->compositor->is_shutting_down()) {
    // wlr_keyboard_finish() (called while tearing down the display, see
    // ~GreeterCompositor()) synthesizes a release for any key still
    // logically held and emits it right here -- touching seat_ this late
    // in teardown is what segfaulted (see is_shutting_down()'s doc
    // comment, compositor.hpp); there is no session left for this event
    // to usefully reach anyway.
    return;
  }
  auto* event = static_cast<wlr_keyboard_key_event*>(data);
  wlr_seat* seat = kb->compositor->seat();
  wlr_seat_set_keyboard(seat, kb->wlr_keyboard_ptr);
  wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
}

void keyboard_destroy(wl_listener* listener, void*) {
  Keyboard* kb = wl_container_of(listener, kb, destroy);
  wl_list_remove(&kb->modifiers.link);
  wl_list_remove(&kb->key.link);
  wl_list_remove(&kb->destroy.link);
  kb->compositor->notify_keyboard_removed();
  delete kb;
}

void make_keyboard(GreeterCompositor* compositor, wlr_keyboard* wlr_kb) {
  auto* kb = new Keyboard{compositor, wlr_kb};

  xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  xkb_keymap* keymap = xkb_keymap_new_from_names(context, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
  wlr_keyboard_set_keymap(wlr_kb, keymap);
  xkb_keymap_unref(keymap);
  xkb_context_unref(context);
  wlr_keyboard_set_repeat_info(wlr_kb, 25, 600);

  kb->modifiers.notify = keyboard_modifiers;
  wl_signal_add(&wlr_kb->events.modifiers, &kb->modifiers);
  kb->key.notify = keyboard_key;
  wl_signal_add(&wlr_kb->events.key, &kb->key);
  kb->destroy.notify = keyboard_destroy;
  wl_signal_add(&wlr_kb->base.events.destroy, &kb->destroy);

  wlr_seat_set_keyboard(compositor->seat(), wlr_kb);
  compositor->notify_keyboard_added();
}

}  // namespace

// -- output --------------------------------------------------------------

void greeter_output_frame(wl_listener* listener, void*);
void greeter_output_request_state(wl_listener* listener, void* data);

void greeter_new_output(wl_listener* listener, void* data) {
  GreeterCompositor* self = wl_container_of(listener, self, new_output_);
  auto* wlr_out = static_cast<wlr_output*>(data);

  wlr_output_init_render(wlr_out, self->allocator_, self->renderer_);

  wlr_output_state state;
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state, true);
  wlr_output_mode* mode = wlr_output_preferred_mode(wlr_out);
  if (mode) {
    wlr_output_state_set_mode(&state, mode);
  }
  wlr_output_commit_state(wlr_out, &state);
  wlr_output_state_finish(&state);

  wlr_output_layout_add_auto(self->output_layout_, wlr_out);
  wlr_scene_output* scene_output = wlr_scene_output_create(self->scene_, wlr_out);
  wlr_scene_output_layout_add_output(
      self->scene_layout_, wlr_output_layout_get(self->output_layout_, wlr_out), scene_output);

  // First output only -- a login screen targets a single display; a
  // second monitor just mirrors whatever the first one shows via the
  // scene graph's own default layout placement, same as any other
  // wlr_scene-based compositor with no explicit per-output content.
  if (!self->output_) {
    self->output_ = wlr_out;
    self->output_width_ = mode ? mode->width : wlr_out->width;
    self->output_height_ = mode ? mode->height : wlr_out->height;
    self->fit_toplevel_to_output();

    // Without this, nothing ever actually renders: wlr_scene_output_create
    // above only sets up the scene<->output link, the wlr_output's own
    // `frame` event (driven by the backend's own repaint/vblank timing)
    // is what has to actually call wlr_scene_output_commit() each frame
    // -- confirmed missing this hangs screencopy (grim) waiting for a
    // frame that never completes, matching src/compositor/output.cpp's
    // own output_frame().
    self->output_frame_.notify = greeter_output_frame;
    wl_signal_add(&wlr_out->events.frame, &self->output_frame_);
    self->output_request_state_.notify = greeter_output_request_state;
    wl_signal_add(&wlr_out->events.request_state, &self->output_request_state_);
  }
}

void greeter_output_frame(wl_listener* listener, void*) {
  GreeterCompositor* self = wl_container_of(listener, self, output_frame_);
  wlr_scene_output* scene_output = wlr_scene_get_scene_output(self->scene_, self->output_);
  if (!scene_output) {
    return;
  }
  wlr_scene_output_commit(scene_output, nullptr);

  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  wlr_scene_output_send_frame_done(scene_output, &now);
}

void greeter_output_request_state(wl_listener* listener, void* data) {
  GreeterCompositor* self = wl_container_of(listener, self, output_request_state_);
  auto* event = static_cast<const wlr_output_event_request_state*>(data);
  wlr_output_commit_state(self->output_, event->state);
}

// -- xdg toplevel (the login client, exactly one expected) ---------------

void greeter_toplevel_map(wl_listener* listener, void*) {
  GreeterCompositor* self = wl_container_of(listener, self, toplevel_map_);
  wlr_scene_node_set_position(&self->toplevel_scene_tree_->node, 0, 0);
  self->fit_toplevel_to_output();
  wlr_seat_keyboard_notify_enter(self->seat_, self->toplevel_->base->surface, nullptr, 0, nullptr);
}

void greeter_toplevel_destroy(wl_listener* listener, void*) {
  GreeterCompositor* self = wl_container_of(listener, self, toplevel_destroy_);
  wl_list_remove(&self->toplevel_map_.link);
  wl_list_remove(&self->toplevel_destroy_.link);
  wl_list_remove(&self->toplevel_surface_commit_.link);
  self->toplevel_ = nullptr;
  self->toplevel_scene_tree_ = nullptr;
}

// wlroots never auto-sends a toplevel's first configure -- every
// wlr_xdg_toplevel_set_*() setter schedules one as a side effect, but
// nothing calls any of them for a brand new toplevel unless the
// compositor does (see src/compositor/server.cpp's identically-named
// listener for the from-source confirmation). Spec-compliant clients
// (GTK4 included) wait for that first configure before attaching a
// buffer, so without this the login client creates its surface and then
// hangs forever with nothing rendered. `initial_commit` is wlroots' flag
// for "the surface just initialized, safe to configure now". Sizing to
// the known output here (rather than server.cpp's 0,0 "client decides")
// avoids an extra resize round-trip since a login screen has exactly one
// reasonable size: fullscreen.
void greeter_toplevel_surface_commit(wl_listener* listener, void*) {
  GreeterCompositor* self = wl_container_of(listener, self, toplevel_surface_commit_);
  if (self->toplevel_ && self->toplevel_->base->initial_commit) {
    self->fit_toplevel_to_output();
  }
}

void greeter_new_xdg_toplevel(wl_listener* listener, void* data) {
  GreeterCompositor* self = wl_container_of(listener, self, new_xdg_toplevel_);
  auto* toplevel = static_cast<wlr_xdg_toplevel*>(data);

  if (self->toplevel_) {
    // The login client is the only thing this compositor ever spawns;
    // anything else asking to open a toplevel here is unexpected --
    // refuse rather than silently juggling a second window with no
    // window-management code to place it.
    wlr_log(WLR_ERROR, "fleetwm-greet: refusing unexpected second toplevel");
    wl_client_destroy(wl_resource_get_client(toplevel->base->resource));
    return;
  }

  self->toplevel_ = toplevel;
  self->toplevel_scene_tree_ = wlr_scene_xdg_surface_create(&self->scene_->tree, toplevel->base);
  // No wlr_xdg_toplevel_set_*() calls here -- toplevel->base is not yet
  // "initialized" (no client commit has happened) at new_toplevel time,
  // and scheduling a configure this early logged "A configure is
  // scheduled for an uninitialized xdg_surface" and left the client
  // hung waiting on a configure/ack sequence that never resolved (no
  // frame ever got composited). fit_toplevel_to_output() in
  // greeter_toplevel_map below does the actual sizing once the surface
  // is real.

  self->toplevel_map_.notify = greeter_toplevel_map;
  wl_signal_add(&toplevel->base->surface->events.map, &self->toplevel_map_);
  self->toplevel_destroy_.notify = greeter_toplevel_destroy;
  wl_signal_add(&toplevel->events.destroy, &self->toplevel_destroy_);
  self->toplevel_surface_commit_.notify = greeter_toplevel_surface_commit;
  wl_signal_add(&toplevel->base->surface->events.commit, &self->toplevel_surface_commit_);
}

void GreeterCompositor::fit_toplevel_to_output() {
  if (!toplevel_ || !output_) {
    return;
  }
  wlr_xdg_toplevel_set_size(toplevel_, output_width_, output_height_);
}

// -- input -----------------------------------------------------------------

void greeter_new_input(wl_listener* listener, void* data) {
  GreeterCompositor* self = wl_container_of(listener, self, new_input_);
  auto* device = static_cast<wlr_input_device*>(data);

  if (device->type == WLR_INPUT_DEVICE_KEYBOARD) {
    make_keyboard(self, wlr_keyboard_from_input_device(device));
  } else if (device->type == WLR_INPUT_DEVICE_POINTER) {
    wlr_cursor_attach_input_device(self->cursor_, device);
  }
}

void greeter_new_virtual_pointer(wl_listener* listener, void* data) {
  GreeterCompositor* self = wl_container_of(listener, self, new_virtual_pointer_);
  auto* event = static_cast<wlr_virtual_pointer_v1_new_pointer_event*>(data);
  wlr_cursor_attach_input_device(self->cursor_, &event->new_pointer->pointer.base);
}

void greeter_new_virtual_keyboard(wl_listener* listener, void* data) {
  GreeterCompositor* self = wl_container_of(listener, self, new_virtual_keyboard_);
  auto* virtual_keyboard = static_cast<wlr_virtual_keyboard_v1*>(data);
  make_keyboard(self, &virtual_keyboard->keyboard);
}

namespace {

// Finds the surface under the cursor -- there is only ever one client's
// worth of scene nodes here (the login card, plus its own popups), so
// no owner-tag scene-node walk (cf. src/compositor/server.cpp's
// scene_node_at) is needed, just the stock wlroots helper.
wlr_surface* surface_at(wlr_scene* scene, double lx, double ly, double* sx, double* sy) {
  wlr_scene_node* node = wlr_scene_node_at(&scene->tree.node, lx, ly, sx, sy);
  if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
    return nullptr;
  }
  wlr_scene_buffer* buffer = wlr_scene_buffer_from_node(node);
  wlr_scene_surface* scene_surface = wlr_scene_surface_try_from_buffer(buffer);
  return scene_surface ? scene_surface->surface : nullptr;
}

void process_cursor_motion(wlr_cursor* cursor, wlr_scene* scene, wlr_seat* seat,
                            wlr_xcursor_manager* cursor_mgr, uint32_t time_msec) {
  double sx = 0, sy = 0;
  wlr_surface* surface = surface_at(scene, cursor->x, cursor->y, &sx, &sy);
  if (surface) {
    wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
    wlr_seat_pointer_notify_motion(seat, time_msec, sx, sy);
  } else {
    wlr_seat_pointer_clear_focus(seat);
    wlr_cursor_set_xcursor(cursor, cursor_mgr, "left_ptr");
  }
}

}  // namespace

void greeter_cursor_motion(wl_listener* listener, void* data) {
  GreeterCompositor* self = wl_container_of(listener, self, cursor_motion_);
  auto* event = static_cast<wlr_pointer_motion_event*>(data);
  wlr_cursor_move(self->cursor_, &event->pointer->base, event->delta_x, event->delta_y);
  process_cursor_motion(self->cursor_, self->scene_, self->seat_, self->cursor_mgr_,
                        event->time_msec);
}

void greeter_cursor_motion_absolute(wl_listener* listener, void* data) {
  GreeterCompositor* self = wl_container_of(listener, self, cursor_motion_absolute_);
  auto* event = static_cast<wlr_pointer_motion_absolute_event*>(data);
  wlr_cursor_warp_absolute(self->cursor_, &event->pointer->base, event->x, event->y);
  process_cursor_motion(self->cursor_, self->scene_, self->seat_, self->cursor_mgr_,
                        event->time_msec);
}

void greeter_cursor_button(wl_listener* listener, void* data) {
  GreeterCompositor* self = wl_container_of(listener, self, cursor_button_);
  auto* event = static_cast<wlr_pointer_button_event*>(data);
  wlr_seat_pointer_notify_button(self->seat_, event->time_msec, event->button, event->state);
}

void greeter_cursor_axis(wl_listener* listener, void* data) {
  GreeterCompositor* self = wl_container_of(listener, self, cursor_axis_);
  auto* event = static_cast<wlr_pointer_axis_event*>(data);
  wlr_seat_pointer_notify_axis(self->seat_, event->time_msec, event->orientation, event->delta,
                                event->delta_discrete, event->source, event->relative_direction);
}

void greeter_cursor_frame(wl_listener* listener, void*) {
  GreeterCompositor* self = wl_container_of(listener, self, cursor_frame_);
  wlr_seat_pointer_notify_frame(self->seat_);
}

void greeter_request_cursor(wl_listener* listener, void* data) {
  GreeterCompositor* self = wl_container_of(listener, self, request_cursor_);
  auto* event = static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);
  wlr_seat_client* focused = self->seat_->pointer_state.focused_client;
  if (focused == event->seat_client) {
    wlr_cursor_set_surface(self->cursor_, event->surface, event->hotspot_x, event->hotspot_y);
  }
}

// -- extra_fd (greeter_ipc socket) integration into the event loop -------

int greeter_extra_fd_readable(int /*fd*/, uint32_t mask, void* data) {
  auto* self = static_cast<GreeterCompositor*>(data);
  if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
    self->stop();
    return 0;
  }
  if (self->extra_fd_callback_) {
    self->extra_fd_callback_();
  }
  return 0;
}

// -- lifecycle -------------------------------------------------------------

GreeterCompositor::GreeterCompositor() = default;

void GreeterCompositor::notify_keyboard_added() {
  ++keyboard_count_;
  uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
  if (keyboard_count_ > 0) {
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
  }
  wlr_seat_set_capabilities(seat_, caps);
}

void GreeterCompositor::notify_keyboard_removed() {
  --keyboard_count_;
  uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
  if (keyboard_count_ > 0) {
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
  }
  wlr_seat_set_capabilities(seat_, caps);
}

GreeterCompositor::~GreeterCompositor() {
  // Must be set before any teardown call below -- see is_shutting_down()'s
  // doc comment (compositor.hpp) for why wl_display_destroy() can still
  // reach back into keyboard_key()/keyboard_modifiers() below this point.
  shutting_down_ = true;
  if (extra_fd_source_) {
    wl_event_source_remove(extra_fd_source_);
  }
  if (display_) {
    wl_display_destroy_clients(display_);
    wl_display_destroy(display_);
  }
}

std::string GreeterCompositor::init() {
  display_ = wl_display_create();

  backend_ = wlr_backend_autocreate(wl_display_get_event_loop(display_), nullptr);
  if (!backend_) {
    return "";
  }
  renderer_ = wlr_renderer_autocreate(backend_);
  if (!renderer_) {
    return "";
  }
  wlr_renderer_init_wl_display(renderer_, display_);
  allocator_ = wlr_allocator_autocreate(backend_, renderer_);
  if (!allocator_) {
    return "";
  }

  compositor_ = wlr_compositor_create(display_, 6, renderer_);
  wlr_subcompositor_create(display_);
  wlr_data_device_manager_create(display_);

  output_layout_ = wlr_output_layout_create(display_);
  scene_ = wlr_scene_create();
  scene_layout_ = wlr_scene_attach_output_layout(scene_, output_layout_);

  new_output_.notify = greeter_new_output;
  wl_signal_add(&backend_->events.new_output, &new_output_);

  xdg_shell_ = wlr_xdg_shell_create(display_, 3);
  new_xdg_toplevel_.notify = greeter_new_xdg_toplevel;
  wl_signal_add(&xdg_shell_->events.new_toplevel, &new_xdg_toplevel_);

  // Same rationale as server.cpp: fleetwm draws no client decorations
  // anywhere, including here.
  decoration_manager_ = wlr_xdg_decoration_manager_v1_create(display_);

  screencopy_manager_ = wlr_screencopy_manager_v1_create(display_);
  xdg_output_manager_ = wlr_xdg_output_manager_v1_create(display_, output_layout_);

  virtual_pointer_manager_ = wlr_virtual_pointer_manager_v1_create(display_);
  new_virtual_pointer_.notify = greeter_new_virtual_pointer;
  wl_signal_add(&virtual_pointer_manager_->events.new_virtual_pointer, &new_virtual_pointer_);
  virtual_keyboard_manager_ = wlr_virtual_keyboard_manager_v1_create(display_);
  new_virtual_keyboard_.notify = greeter_new_virtual_keyboard;
  wl_signal_add(&virtual_keyboard_manager_->events.new_virtual_keyboard, &new_virtual_keyboard_);

  cursor_ = wlr_cursor_create();
  wlr_cursor_attach_output_layout(cursor_, output_layout_);
  cursor_mgr_ = wlr_xcursor_manager_create(nullptr, 24);

  cursor_motion_.notify = greeter_cursor_motion;
  wl_signal_add(&cursor_->events.motion, &cursor_motion_);
  cursor_motion_absolute_.notify = greeter_cursor_motion_absolute;
  wl_signal_add(&cursor_->events.motion_absolute, &cursor_motion_absolute_);
  cursor_button_.notify = greeter_cursor_button;
  wl_signal_add(&cursor_->events.button, &cursor_button_);
  cursor_axis_.notify = greeter_cursor_axis;
  wl_signal_add(&cursor_->events.axis, &cursor_axis_);
  cursor_frame_.notify = greeter_cursor_frame;
  wl_signal_add(&cursor_->events.frame, &cursor_frame_);

  new_input_.notify = greeter_new_input;
  wl_signal_add(&backend_->events.new_input, &new_input_);

  seat_ = wlr_seat_create(display_, "seat0");
  wlr_seat_set_capabilities(seat_, WL_SEAT_CAPABILITY_POINTER);
  request_cursor_.notify = greeter_request_cursor;
  wl_signal_add(&seat_->events.request_set_cursor, &request_cursor_);

  const char* socket = wl_display_add_socket_auto(display_);
  if (!socket) {
    return "";
  }

  if (!wlr_backend_start(backend_)) {
    return "";
  }

  return socket;
}

void GreeterCompositor::run(int extra_fd, std::function<void()> on_extra_fd_readable) {
  extra_fd_callback_ = std::move(on_extra_fd_readable);
  wl_event_loop* loop = wl_display_get_event_loop(display_);
  extra_fd_source_ = wl_event_loop_add_fd(loop, extra_fd, WL_EVENT_READABLE,
                                           greeter_extra_fd_readable, this);
  wl_display_run(display_);
}

void GreeterCompositor::stop() {
  wl_display_terminate(display_);
}

}  // namespace fleetwm::greeter
