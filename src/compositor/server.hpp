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

#include <xkbcommon/xkbcommon.h>

#include <sys/types.h>

#include <list>
#include <memory>
#include <vector>

#include "config.h"
#include "default_apps.hpp"
#include "keybinds_config.hpp"
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
  // Above layer_top_ (the bar) but below layer_overlay_ -- fullscreen
  // views live here instead, so a fullscreened app visually covers the
  // bar (matching normal fullscreen expectations) while a genuine
  // layer-shell overlay client (fleetwm-locker, fleetwm-launcher) still
  // stays on top of it, same as any other desktop's z-order. See
  // View::set_fullscreen().
  wlr_scene_tree* layer_fullscreen() const { return layer_fullscreen_; }
  // Topmost layer, above even layer_overlay_ -- exists only for the
  // per-frame debug overlay (Output::update_debug_overlay(), toggled by
  // toggle_debug_overlay()) so it's never hidden behind a real
  // layer-shell overlay client. Nothing else should parent nodes here.
  wlr_scene_tree* layer_debug() const { return layer_debug_; }

  bool debug_overlay_enabled() const { return debug_overlay_enabled_; }
  // Alt+Shift+<keybinds.toggle_debug_overlay> (default "I") -- flips a
  // single global on/off switch for every output's frame-time bar
  // graph. Deliberately one flag for all outputs rather than per-output
  // state: this is a developer/debugging tool, not a per-monitor user
  // preference.
  void toggle_debug_overlay();

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

  // Current default_apps.toml contents (currently just terminal_command,
  // the one default-app choice with no XDG mimetype -- see
  // default_apps.hpp), kept fresh by the same inotify watch as
  // theme_config_ since both files live in the same config directory.
  const DefaultAppsConfig& default_apps_config() const { return default_apps_config_; }
  void reload_default_apps_config() { default_apps_config_ = load_default_apps_config(); }

  // Every field of KeybindsConfig (keybinds_config.hpp) is a keysym
  // *name* (human-editable in keybinds.toml); this is the resolved
  // xkb_keysym_t form input.cpp's Keyboard::handle_keybind() actually
  // compares against on every keypress, cached here so that comparison
  // doesn't re-parse a string on every single key event. Defaults match
  // this codebase's previous hardcoded constexpr keysym constants
  // exactly, so an unconfigured install behaves identically to before
  // this became remappable.
  struct ResolvedKeybinds {
    xkb_keysym_t terminal = XKB_KEY_Return;
    xkb_keysym_t launcher = XKB_KEY_d;
    xkb_keysym_t close_window = XKB_KEY_Q;
    xkb_keysym_t toggle_pin = XKB_KEY_P;
    xkb_keysym_t toggle_float = XKB_KEY_F;
    xkb_keysym_t lock = XKB_KEY_L;
    xkb_keysym_t screenshot = XKB_KEY_S;
    xkb_keysym_t focus_left = XKB_KEY_h;
    xkb_keysym_t focus_down = XKB_KEY_j;
    xkb_keysym_t focus_up = XKB_KEY_k;
    xkb_keysym_t focus_right = XKB_KEY_l;
    xkb_keysym_t quit = XKB_KEY_Escape;
    xkb_keysym_t debug_overlay = XKB_KEY_I;
  };
  const ResolvedKeybinds& keybinds() const { return resolved_keybinds_; }
  void reload_keybinds_config();

  // Screen-lock state (bar's power-menu "Lock" action, see
  // bar_window.cpp's build_power_menu()/on_power_action()). `locked_`
  // gates input.cpp's Alt+<key> global keybind interception (input.cpp's
  // keyboard_key()) so a locked session can't be bypassed by e.g.
  // Alt+Return spawning a terminal -- everything else (ordinary key
  // events, all pointer events) already flows to whatever surface holds
  // seat keyboard/pointer focus regardless of locked_, which is safe here
  // only because fleetwm-locker's lock surface is layer-shell OVERLAY +
  // KEYBOARD_MODE_EXCLUSIVE and anchored fullscreen (see locker_window.cpp),
  // so it already owns focus and fully occludes every toplevel underneath.
  bool is_locked() const { return locked_; }

  // Spawns fleetwm-locker and sets locked_ = true. A no-op if already
  // locked (e.g. a second "LOCK" IPC command while one lock screen is
  // already up) -- does not spawn a second locker process on top of the
  // first. See ipc_server.cpp's "LOCK" command handling.
  void request_lock();

  // Only succeeds if currently locked *and* `requesting_pid` is the exact
  // pid request_lock() spawned -- verified by the caller (ipc_server.cpp's
  // "UNLOCK" handling) via SO_PEERCRED on the requesting socket, which the
  // kernel supplies and a client process cannot spoof. This is what stops
  // any other process on the same IPC socket from just sending "UNLOCK"
  // itself without ever having passed fleetwm-locker's PAM check. Returns
  // true if the unlock was accepted.
  bool confirm_unlock(pid_t requesting_pid);

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
  // Not one of the four wlr-layer-shell-v1 protocol layers -- fleetwm's
  // own addition, holding whichever View is currently fullscreen (see
  // View::set_fullscreen()). Positioned above layer_top_ so a
  // fullscreened app covers the bar, below layer_overlay_ so a genuine
  // layer-shell overlay client still stays on top of it.
  wlr_scene_tree* layer_fullscreen_ = nullptr;
  wlr_scene_tree* layer_overlay_ = nullptr;
  wlr_scene_tree* layer_debug_ = nullptr;

  bool debug_overlay_enabled_ = false;

  wlr_scene_tree* layer_tree_for(zwlr_layer_shell_v1_layer layer);

  // Sets up the inotify watch backing reload_theme_config()'s live
  // reload. Returns false (non-fatal -- theming just won't live-update)
  // on any setup failure.
  bool start_theme_watch();

  // Registers SIGTERM/SIGINT handlers (via wl_event_loop_add_signal,
  // the safe wayland-server-integrated way to handle a signal -- no
  // traditional async-signal-safety constraints, it delivers via the
  // event loop like any other source) that call wl_display_terminate()
  // for a clean shutdown instead of the OS's default "just die"
  // disposition. Without this, `kill`/`systemctl stop`/a plain Ctrl-C
  // from a terminal all skipped every atexit-registered cleanup
  // entirely -- confirmed missing while setting up PGO training
  // (scripts/build-pgo.sh): GCC's profiling runtime flushes collected
  // .gcda data via exactly such an atexit hook, so every PGO training
  // session ended via `kill` was silently losing its whole profile.
  // Same clean-exit path Alt+Escape's existing keybind already uses
  // (input.cpp calls the same wl_display_terminate()), just reachable
  // without a keyboard now too.
  void start_signal_handlers();

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
  DefaultAppsConfig default_apps_config_;
  KeybindsConfig keybinds_config_;
  ResolvedKeybinds resolved_keybinds_;
  bool locked_ = false;
  // pid of the currently-spawned fleetwm-locker, or -1 when not locked.
  // Not reaped via waitpid (matches spawn_autostart()'s existing
  // no-reaping convention, server.cpp) -- an unreaped locker becomes a
  // zombie for the rest of the compositor's lifetime once it exits, same
  // trade-off already made for fleetwm-bar/fleetwm-wallpaper.
  pid_t locker_pid_ = -1;
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

  wl_event_source* sigterm_source_ = nullptr;
  wl_event_source* sigint_source_ = nullptr;
  wl_event_source* sigchld_source_ = nullptr;

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
