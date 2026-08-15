#pragma once

#include <wayland-server-core.h>

extern "C" {
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
}

#include <list>
#include <memory>
#include <vector>

#include "config.h"
#include "workspace.hpp"

#if FLEETWM_XWAYLAND
extern "C" {
#include <wlr/xwayland.h>
}
#endif

namespace fleetwm {

class Output;
class View;
class IpcServer;

// Server owns every long-lived wlroots object and drives the whole
// compositor. This mirrors wlroots' tinywl.c example structure (one big
// "server" struct with wl_listeners hung off each subsystem) translated to
// C++ member functions instead of free functions + void* data, since that
// keeps the listener callbacks trivially able to reach back into typed
// state without the casts tinywl.c needs in C.
class Server {
 public:
  Server();
  ~Server();

  // Creates the wl_display, initializes the wlroots backend/renderer/
  // allocator, sets up the scene graph, and starts listening on a Wayland
  // socket. Returns false on any initialization failure.
  bool init();

  // Runs the wl_display event loop until told to stop (e.g. SIGTERM, or an
  // internal request from a keybind). Blocks the calling thread.
  void run();

  wl_display* display() const { return display_; }
  wlr_scene* scene() const { return scene_; }
  wlr_output_layout* output_layout() const { return output_layout_; }
  wlr_seat* seat() const { return seat_; }
  wlr_cursor* cursor() const { return cursor_; }

  // Focuses `view`, raising it in the scene graph and handing keyboard
  // focus to its surface. Passing nullptr clears focus.
  void focus_view(View* view);

  // Looks up the Workspace object for `output`'s currently-active
  // workspace slot (1-9, 0 -> index 9). Per the per-output workspace model
  // (ADR 0002), each Output owns its own set of 10 Workspace objects, so
  // this is a thin forwarding call kept on Server for callers (input.cpp
  // keybind handling, ipc_server.cpp) that don't already hold an Output*.
  Workspace* active_workspace_for_focused_output();

  std::list<std::unique_ptr<View>> views;  // stacking order: front = topmost
  std::vector<std::unique_ptr<Output>> outputs;

  std::unique_ptr<IpcServer> ipc_server;

 private:
  wl_display* display_ = nullptr;
  wlr_backend* backend_ = nullptr;
  wlr_renderer* renderer_ = nullptr;
  wlr_allocator* allocator_ = nullptr;
  wlr_compositor* compositor_ = nullptr;
  wlr_scene* scene_ = nullptr;
  wlr_scene_output_layout* scene_layout_ = nullptr;
  wlr_output_layout* output_layout_ = nullptr;

  wlr_xdg_shell* xdg_shell_ = nullptr;
  wl_listener new_xdg_toplevel_{};

  wlr_cursor* cursor_ = nullptr;
  wlr_xcursor_manager* cursor_mgr_ = nullptr;
  wlr_seat* seat_ = nullptr;

  wl_listener new_output_{};
  wl_listener new_input_{};
  wl_listener request_cursor_{};
  wl_listener request_set_selection_{};

  wl_listener cursor_motion_{};
  wl_listener cursor_motion_absolute_{};
  wl_listener cursor_button_{};
  wl_listener cursor_axis_{};
  wl_listener cursor_frame_{};

#if FLEETWM_XWAYLAND
  wlr_xwayland* xwayland_ = nullptr;
  wl_listener new_xwayland_surface_{};
#endif

  friend void server_new_output(wl_listener* listener, void* data);
  friend void server_new_xdg_toplevel(wl_listener* listener, void* data);
  friend void server_new_input(wl_listener* listener, void* data);
  friend void server_cursor_motion(wl_listener* listener, void* data);
  friend void server_cursor_motion_absolute(wl_listener* listener, void* data);
  friend void server_cursor_button(wl_listener* listener, void* data);
  friend void server_cursor_axis(wl_listener* listener, void* data);
  friend void server_cursor_frame(wl_listener* listener, void* data);
  friend void server_request_cursor(wl_listener* listener, void* data);
  friend void server_request_set_selection(wl_listener* listener, void* data);
};

}  // namespace fleetwm
