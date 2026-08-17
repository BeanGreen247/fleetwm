#pragma once

#include <wayland-server-core.h>

extern "C" {
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
}

#include <functional>
#include <string>

namespace fleetwm::greeter {

// Minimal single-client wlroots compositor that hosts the login card UI
// (a GTK4 client this process spawns -- src/greeter-login) before any real
// user session exists. Deliberately far smaller than fleetwm::Server
// (src/compositor/server.{hpp,cpp}): no layer-shell, no workspaces/tiling,
// no IPC server, no multi-window management -- exactly one xdg_toplevel is
// ever expected (the login client), always kept fullscreen and focused.
// This whole binary runs as root for its entire lifetime (ADR 0006), so
// every subsystem added here directly widens the root-owned attack
// surface; keep additions to what a login screen strictly needs.
class GreeterCompositor {
 public:
  GreeterCompositor();
  ~GreeterCompositor();
  GreeterCompositor(const GreeterCompositor&) = delete;
  GreeterCompositor& operator=(const GreeterCompositor&) = delete;

  // Starts the wlroots backend/renderer/scene and opens a Wayland socket.
  // Returns the WAYLAND_DISPLAY socket name on success, or an empty string
  // on failure.
  std::string init();

  // Runs the event loop until stop() is called (typically once auth
  // succeeds) or the login client's connection is lost. `extra_fd` (the
  // greeter_ipc socket to the login client) is integrated into the same
  // wl_event_loop via wl_event_loop_add_fd, mirroring the inotify-watch
  // integration pattern in server.cpp's start_theme_watch(); the callback
  // is invoked whenever it becomes readable.
  void run(int extra_fd, std::function<void()> on_extra_fd_readable);

  // Stops run()'s event loop (safe to call from within an extra_fd
  // callback).
  void stop();

  wlr_seat* seat() const { return seat_; }

  // Keeps the seat's advertised capabilities in sync with whether any
  // keyboard is currently attached -- wlr_seat itself doesn't track this
  // (same reason src/compositor/server.{hpp,cpp} has an identical pair
  // of methods). Without ever calling wlr_seat_set_capabilities at all,
  // the seat advertises none, and clients (GTK4 included) never route
  // key/pointer input at the protocol level even though the compositor
  // itself is forwarding events -- confirmed via real testing: wtype
  // input was silently dropped until this was added.
  void notify_keyboard_added();
  void notify_keyboard_removed();

  // True from the first line of ~GreeterCompositor() onward. Guards
  // keyboard_key()/keyboard_modifiers() (compositor.cpp): wl_display_destroy()
  // cascades into wlr_keyboard_finish() for every still-attached keyboard,
  // which itself emits a synthetic "key" event to release any key that
  // was logically still held (e.g. the Enter that just submitted the
  // login form, if its release hasn't been processed yet) -- but by that
  // point in the same teardown cascade, seat_ may already be a dangling
  // pointer (wlroots does not guarantee the seat's own destroy fires
  // strictly after every input device's), so calling wlr_seat_
  // keyboard_notify_key() on it segfaults. Confirmed via gdb: crash was
  // in wlr_seat_keyboard_notify_key, called from wlr_keyboard_finish,
  // called from wl_display_destroy, called from ~GreeterCompositor.
  bool is_shutting_down() const { return shutting_down_; }

 private:
  friend void greeter_new_output(wl_listener*, void*);
  friend void greeter_output_frame(wl_listener*, void*);
  friend void greeter_output_request_state(wl_listener*, void*);
  friend void greeter_new_xdg_toplevel(wl_listener*, void*);
  friend void greeter_new_input(wl_listener*, void*);
  friend void greeter_new_virtual_pointer(wl_listener*, void*);
  friend void greeter_new_virtual_keyboard(wl_listener*, void*);
  friend void greeter_cursor_motion(wl_listener*, void*);
  friend void greeter_cursor_motion_absolute(wl_listener*, void*);
  friend void greeter_cursor_button(wl_listener*, void*);
  friend void greeter_cursor_axis(wl_listener*, void*);
  friend void greeter_cursor_frame(wl_listener*, void*);
  friend void greeter_request_cursor(wl_listener*, void*);
  friend void greeter_toplevel_map(wl_listener*, void*);
  friend void greeter_toplevel_destroy(wl_listener*, void*);
  friend void greeter_toplevel_surface_commit(wl_listener*, void*);
  friend int greeter_extra_fd_readable(int fd, uint32_t mask, void* data);

  // Sizes and focuses the sole toplevel to fill the first known output.
  void fit_toplevel_to_output();

  wl_display* display_ = nullptr;
  wlr_backend* backend_ = nullptr;
  wlr_renderer* renderer_ = nullptr;
  wlr_allocator* allocator_ = nullptr;
  wlr_compositor* compositor_ = nullptr;
  wlr_output_layout* output_layout_ = nullptr;
  wlr_scene* scene_ = nullptr;
  wlr_scene_output_layout* scene_layout_ = nullptr;
  wlr_xdg_shell* xdg_shell_ = nullptr;
  wlr_xdg_decoration_manager_v1* decoration_manager_ = nullptr;
  wlr_cursor* cursor_ = nullptr;
  wlr_xcursor_manager* cursor_mgr_ = nullptr;
  wlr_seat* seat_ = nullptr;
  int keyboard_count_ = 0;
  bool shutting_down_ = false;

  // Read-only/synthetic-input protocols, same rationale as
  // src/compositor/server.hpp's own copies: wlr-screencopy + xdg-output
  // let `grim` capture this screen, and virtual-pointer/virtual-keyboard
  // let `wlrctl`/`wtype` drive it -- both essential for verifying the
  // login flow over SSH with no physical console access.
  wlr_screencopy_manager_v1* screencopy_manager_ = nullptr;
  wlr_xdg_output_manager_v1* xdg_output_manager_ = nullptr;
  wlr_virtual_pointer_manager_v1* virtual_pointer_manager_ = nullptr;
  wlr_virtual_keyboard_manager_v1* virtual_keyboard_manager_ = nullptr;
  wl_listener new_virtual_pointer_{};
  wl_listener new_virtual_keyboard_{};

  wlr_output* output_ = nullptr;  // first output only; a login screen is single-display
  int output_width_ = 0;
  int output_height_ = 0;

  // The sole login-client toplevel, once it has mapped. wlr_xdg_toplevel
  // owns no stable identity beyond its lifetime, so this is cleared on
  // both unmap-equivalent (destroy) and never re-set to a second client
  // (there should never be one).
  wlr_xdg_toplevel* toplevel_ = nullptr;
  wlr_scene_tree* toplevel_scene_tree_ = nullptr;

  wl_listener new_output_{};
  wl_listener output_frame_{};
  wl_listener output_request_state_{};
  wl_listener new_xdg_toplevel_{};
  wl_listener toplevel_map_{};
  wl_listener toplevel_destroy_{};
  wl_listener toplevel_surface_commit_{};
  wl_listener new_input_{};
  wl_listener cursor_motion_{};
  wl_listener cursor_motion_absolute_{};
  wl_listener cursor_button_{};
  wl_listener cursor_axis_{};
  wl_listener cursor_frame_{};
  wl_listener request_cursor_{};

  wl_event_source* extra_fd_source_ = nullptr;
  std::function<void()> extra_fd_callback_;
};

}  // namespace fleetwm::greeter
