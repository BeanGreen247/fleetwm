#pragma once

#include <wayland-server-core.h>

extern "C" {
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_layer_shell_v1.h>
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

#include <list>
#include <memory>
#include <vector>

#include "config.h"
#include "theme.hpp"
#include "workspace.hpp"

#if FLEETWM_XWAYLAND
extern "C" {
#include <wlr/xwayland.h>
}
#endif

namespace fleetwm {

class Output;
class View;
class LayerSurface;
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
  wlr_scene_tree* layer_toplevels() const { return layer_toplevels_; }
  // Always-enabled, above layer_toplevels_ but below layer_top_/
  // layer_overlay_ -- pinned views live here instead, so they're never
  // touched by Output::switch_workspace() (visible across every
  // workspace) while still sitting under layer-shell popups like the
  // launcher. See View::set_pinned().
  wlr_scene_tree* layer_pinned() const { return layer_pinned_; }

  // Focuses `view`, raising it in the scene graph and handing keyboard
  // focus to its surface. Passing nullptr clears focus.
  void focus_view(View* view);

  // Grants seat keyboard focus to a layer-shell surface that requested
  // keyboard interactivity (e.g. a launcher popup). Unlike focus_view,
  // this does not raise/activate/reorder anything -- layer surfaces are
  // already top of their own layer.
  void focus_layer_surface(LayerSurface* layer_surface);

  // Sets the cursor to the default ("left_ptr") xcursor image. Called when
  // the pointer moves over no view (e.g. bare background).
  void set_default_cursor_image();

  // Called by Keyboard's constructor/destructor (input.cpp) to keep the
  // seat's advertised capabilities in sync with whether any keyboard is
  // currently attached -- wlr_seat itself doesn't track this, unlike
  // earlier wlroots versions' tinywl.c example server struct.
  void notify_keyboard_added();
  void notify_keyboard_removed();

  // Looks up the Workspace object for `output`'s currently-active
  // workspace slot (1-9, 0 -> index 9). Per the per-output workspace model
  // (ADR 0002), each Output owns its own set of 10 Workspace objects, so
  // this is a thin forwarding call kept on Server for callers (input.cpp
  // keybind handling, ipc_server.cpp) that don't already hold an Output*.
  Workspace* active_workspace_for_focused_output();

  // Finds the Output wrapping a given wlr_output*, or nullptr if none
  // (e.g. the output was already destroyed). Used by layer-shell exclusive-
  // zone handling (layer_surface.cpp) to route from wlr_layer_surface_v1::
  // output back to the owning Output for update_usable_area().
  Output* output_for(wlr_output* wlr_output_ptr) const;

  // Current theme.toml contents, loaded at init() and kept fresh by an
  // inotify watch on the config file (see theme_watch_fd_ below) -- any
  // write to theme.toml, from fleetwm-settings or anything else, is
  // picked up live without needing a re-login or an explicit IPC ping.
  const ThemeConfig& theme_config() const { return theme_config_; }

  // Re-reads theme.toml into theme_config_ and refreshes every current
  // View's border (color/thickness may have changed). Called once at
  // init() and again on every inotify-detected write to the config file.
  void reload_theme_config();

  std::list<std::unique_ptr<View>> views;  // stacking order: front = topmost
  std::list<std::unique_ptr<LayerSurface>> layer_surfaces;
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

  // Always-enabled z-order layers, bottom to top; child of scene_->tree in
  // this creation order. layer_toplevels_ hosts every View's
  // container_tree (see server_new_xdg_toplevel); layer_pinned_ hosts
  // pinned views' container_tree instead (see View::set_pinned) so they
  // survive Output::switch_workspace(); the other four correspond 1:1 to
  // the wlr-layer-shell-v1 protocol layers and are never touched by
  // Output::switch_workspace() -- layer surfaces persist across workspace
  // switches by construction.
  wlr_scene_tree* layer_background_ = nullptr;
  wlr_scene_tree* layer_bottom_ = nullptr;
  wlr_scene_tree* layer_toplevels_ = nullptr;
  wlr_scene_tree* layer_pinned_ = nullptr;
  wlr_scene_tree* layer_top_ = nullptr;
  wlr_scene_tree* layer_overlay_ = nullptr;

  wlr_scene_tree* layer_tree_for(zwlr_layer_shell_v1_layer layer);

  // Sets up the inotify watch backing reload_theme_config()'s live
  // reload. Returns false (non-fatal -- theming just won't live-update)
  // on any setup failure.
  bool start_theme_watch();

  wlr_xdg_shell* xdg_shell_ = nullptr;
  wl_listener new_xdg_toplevel_{};

  // xdg-decoration-unstable-v1: tells clients to use server-side
  // decorations instead of drawing their own CSDs. fleetwm draws no
  // decorations at all (no titlebar, no buttons) -- forcing SERVER_SIDE
  // mode means "compositor is responsible for decorations" is true in
  // the protocol sense, and since the compositor draws none, the net
  // result is borderless windows without patching every client.
  wlr_xdg_decoration_manager_v1* decoration_manager_ = nullptr;
  wl_listener new_toplevel_decoration_{};

  wlr_layer_shell_v1* layer_shell_ = nullptr;
  wl_listener new_layer_surface_{};

  // wlr-screencopy-unstable-v1: lets clients like grim capture the
  // screen. No manual wiring needed beyond creation -- wlroots handles
  // the whole protocol internally via the scene graph.
  wlr_screencopy_manager_v1* screencopy_manager_ = nullptr;
  // xdg-output-unstable-v1: lets clients (grim included) query real
  // output geometry -- without it grim can't determine capture
  // dimensions at all and fails outright.
  wlr_xdg_output_manager_v1* xdg_output_manager_ = nullptr;
  // wlr-virtual-pointer-unstable-v1: lets tools like wlrctl inject
  // synthetic pointer motion/button/axis events, same as a real input
  // device would -- used for scripted UI testing over SSH where no
  // physical mouse is available. new_virtual_pointer hands back a
  // wlr_virtual_pointer_v1 whose embedded wlr_pointer.base is a real
  // wlr_input_device, so it's routed through the same
  // wlr_cursor_attach_input_device() path as server_new_input's
  // WLR_INPUT_DEVICE_POINTER branch rather than needing separate logic.
  wlr_virtual_pointer_manager_v1* virtual_pointer_manager_ = nullptr;
  wl_listener new_virtual_pointer_{};
  // wlr-virtual-keyboard-unstable-v1: same rationale as
  // virtual_pointer_manager_ above, but for synthetic key events (wtype).
  wlr_virtual_keyboard_manager_v1* virtual_keyboard_manager_ = nullptr;
  wl_listener new_virtual_keyboard_{};

  wlr_cursor* cursor_ = nullptr;
  wlr_xcursor_manager* cursor_mgr_ = nullptr;
  wlr_seat* seat_ = nullptr;
  int keyboard_count_ = 0;

  ThemeConfig theme_config_;
  // inotify fd watching theme.toml's parent directory (not the file
  // itself -- fleetwm-settings' save_theme_config() writes via a fresh
  // std::ofstream each time, which some inotify setups see as the watched
  // file being replaced rather than modified in place; watching the
  // directory for IN_CLOSE_WRITE/IN_MOVED_TO on that specific filename
  // catches both a plain in-place write and an atomic rename-into-place
  // save). Wired into the compositor's existing wl_event_loop via
  // wl_event_loop_add_fd(), same integration pattern IpcServer already
  // uses for its listen/client sockets.
  int theme_watch_fd_ = -1;
  wl_event_source* theme_watch_source_ = nullptr;

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
  friend void server_new_layer_surface(wl_listener* listener, void* data);
  friend void server_new_toplevel_decoration(wl_listener* listener, void* data);
  friend void server_new_input(wl_listener* listener, void* data);
  friend void server_new_virtual_pointer(wl_listener* listener, void* data);
  friend void server_new_virtual_keyboard(wl_listener* listener, void* data);
  friend int server_theme_watch_readable(int fd, uint32_t mask, void* data);
  friend void server_cursor_motion(wl_listener* listener, void* data);
  friend void server_cursor_motion_absolute(wl_listener* listener, void* data);
  friend void server_cursor_button(wl_listener* listener, void* data);
  friend void server_cursor_axis(wl_listener* listener, void* data);
  friend void server_cursor_frame(wl_listener* listener, void* data);
  friend void server_request_cursor(wl_listener* listener, void* data);
  friend void server_request_set_selection(wl_listener* listener, void* data);
};

}  // namespace fleetwm
