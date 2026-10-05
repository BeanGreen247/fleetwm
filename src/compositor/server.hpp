#pragma once

#include <wayland-server-core.h>

extern "C" {
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_tearing_control_v1.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
}

#include <xkbcommon/xkbcommon.h>

#include <sys/types.h>

#include <chrono>
#include <list>

#include "output_config.hpp"
#include "power_config.hpp"
#include <memory>
#include <vector>

#include "config.h"
#include "default_apps.hpp"
#include "keybinds_config.hpp"
#include "theme.hpp"
#include "window_geometry.hpp"
#include "window_list.hpp"
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
// Shared by xdg-shell and X11 windows (see server.cpp): a window that just appeared on screen
// is placed, tiled and focused; one that went away is removed and focus moves on.
void view_mapped(View* view);
void view_unmapped(View* view);
void server_new_xwayland_surface(wl_listener* listener, void* data);

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
  wlr_renderer* renderer() const { return renderer_; }
  wlr_scene* scene() const { return scene_; }
  wlr_output_layout* output_layout() const { return output_layout_; }
  wlr_seat* seat() const { return seat_; }
  wlr_cursor* cursor() const { return cursor_; }
  wlr_xcursor_manager* cursor_manager() const { return cursor_mgr_; }
  wlr_scene_tree* layer_toplevels() const { return layer_toplevels_; }
  // Always-enabled, above layer_toplevels_ but below layer_top_/
  // layer_overlay_ -- pinned views live here instead, so they're never
  // touched by Output::switch_workspace() (visible across every
  // workspace) while still sitting under layer-shell popups like the
  // launcher. See View::set_pinned().
  wlr_scene_tree* layer_pinned() const { return layer_pinned_; }
  // Above pinned windows, below the bar: Settings and dialogs, which no other window may cover.
  wlr_scene_tree* layer_topmost() const { return layer_topmost_; }
  wlr_tearing_control_manager_v1* tearing_manager() const { return tearing_manager_; }
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
  // Sets a named xcursor image unless it is already the one showing; pointer
  // motion calls this on every event, and re-loading the image each time was
  // measurable work. A client-provided cursor (request_set_cursor) resets it.
  void set_cursor_name(const char* name);
  // A client asked (wp_cursor_shape_v1) for a named cursor shape: show it from our theme.
  void apply_cursor_shape(wlr_seat_client* client, const char* name);

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

  // ---- Desktop (floating) layout: interactive move/resize ----
  bool desktop_layout() const { return theme_config_.window_layout == WindowLayout::Desktop; }
  bool grab_active() const { return grab_mode_ != GrabMode::None; }
  View* grab_view() const { return grab_view_; }
  // Starts dragging `view` with the pointer (titlebar drag or a client's
  // xdg_toplevel.move request) / resizing it from `edges` (WLR_EDGE_* mask).
  void begin_move(View* view);
  void begin_resize(View* view, uint32_t edges);
  // Follows the cursor while a grab is active.
  void update_grab();
  void end_grab();
  // Forgets `view` everywhere it is remembered by pointer (grab, hover).
  void forget_view(View* view);
  // Titlebar button hover bookkeeping: clears the old view's highlight.
  void set_hover_view(View* view);
  void toggle_maximize(View* view);
  void minimize_view(View* view);

  // ---- idle: display off and sleep (Settings -> Power) ----
  void init_idle();
  // Call on every key press, pointer move or click: restarts the idle clock and wakes
  // the displays if they were blanked.
  void note_activity();
  void reload_power_config();
  void on_idle_timer();
  void set_displays_blanked(bool blanked);
  bool displays_blanked() const { return displays_blanked_; }

  // ---- keyboard window management (cycling, snapping, workspaces, screens) ----
  // Alt+Tab-style cycling in most-recently-used order. `hold_mask` is the modifier
  // bits whose release ends the cycle (the Alt in Alt+Tab).
  void cycle_windows(bool backward, unsigned hold_mask);
  void end_window_cycle() { cycle_order_.clear(); cycle_hold_mask_ = 0; }
  bool cycling() const { return !cycle_order_.empty(); }
  unsigned cycle_hold_mask() const { return cycle_hold_mask_; }
  // Windows-style whole-desktop actions on the workspace being looked at.
  void show_desktop_toggle();   // minimize everything; again: bring those windows back
  void minimize_all();
  void restore_all();
  // Super+arrow: the Windows-style snap step for the focused window.
  void snap_step_focused(geom::Direction dir);
  // Switch to workspace `index` (0-9) on the focused window's output, or the first output.
  void switch_workspace(int index);
  void switch_workspace_relative(int delta);
  // Move `view` to workspace `index` of its output; it stays where you are looking.
  void move_view_to_workspace(View* view, int index);
  // Send `view` to the neighbouring screen (-1 previous, +1 next, ordered left to right).
  // Returns false when there is no such screen.
  bool move_view_to_screen(View* view, int delta);
  View* focused_view_for_actions() const;

  // ---- window list for taskbar clients (IPC) ----
  // Gives focus to the topmost visible window other than `gone` (focus-on-close
  // and focus-on-minimize), or clears focus if there is none.
  void focus_next_after(View* gone);
  View* view_by_id(uint32_t id) const;
  // Brings a window to the front: restores it if minimized, then focuses it.
  void activate_view(View* view);
  // Taskbar button semantics: restore if minimized, minimize if it already has
  // focus, otherwise focus it.
  void toggle_view_from_taskbar(View* view);
  // Coalesces any number of changes within one event-loop iteration into a
  // single WINDOWS broadcast.
  void schedule_windows_broadcast();
  void broadcast_windows_now();
  std::vector<WindowEntry> window_snapshot() const;
  // Titlebar double-click detection (button press time, ms).
  bool is_double_click(View* view, uint32_t time_msec);
  bool swallow_release = false;  // a decoration press was consumed; eat its release

  // pid of the Xwayland process wlroots manages (0 if none), so the SIGCHLD
  // reaper leaves its exit status for wlroots.
  pid_t xwayland_server_pid() const;

  // ---- display management (resolution + position of each monitor) ----
  struct ModeInfo {
    int width = 0, height = 0, refresh_mhz = 0;
    bool current = false, preferred = false;
  };
  struct OutputInfo {
    std::string name;
    int x = 0, y = 0, width = 0, height = 0, refresh_mhz = 0;
    std::vector<ModeInfo> modes;
  };
  std::vector<OutputInfo> describe_outputs() const;
  // Applies (and persists to outputs.toml) a mode and/or position for the
  // named output. Returns false with a message in *error if the output is
  // unknown or the mode is rejected; nothing is changed or saved then.
  // Reverts an output to the mode it had before the last apply_output_setting().
  void revert_output_mode(Output* output);
  bool apply_output_setting(const std::string& name, const OutputSetting& setting,
                            std::string* error);

  // Current theme.toml contents, loaded at init() and kept fresh by an
  // inotify watch on the config file (see theme_watch_fd_ below) -- any
  // write to theme.toml, from fleetwm-settings or anything else, is
  // picked up live without needing a re-login or an explicit IPC ping.
  const ThemeConfig& theme_config() const { return theme_config_; }
  // The three window border colours, parsed once per theme load instead of on every client
  // commit (resize_border runs on each one).
  struct BorderColors {
    float pinned_focused[4], pinned[4], focus[4];
  };
  const BorderColors& border_colors() const { return border_colors_; }
  void refresh_border_colors();
  void update_app_appearance();

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
    // Resolved "ctrl+alt+t"-style combos (see KeybindsConfig): modifier bits + keysym.
    struct Combo {
      unsigned mods = 0;
      xkb_keysym_t sym = XKB_KEY_NoSymbol;
    };
    Combo shortcuts_help{kModLogo, XKB_KEY_slash};
    Combo desktop_terminal{kModCtrl | kModAlt, XKB_KEY_t};
    Combo desktop_browser{kModLogo | kModShift, XKB_KEY_b};
    Combo desktop_file_manager{kModLogo | kModShift, XKB_KEY_e};
    Combo desktop_text_editor{kModLogo | kModShift, XKB_KEY_t};
    Combo desktop_debug_overlay{kModCtrl | kModAlt, XKB_KEY_i};
    Combo desktop_snap_left{kModLogo, XKB_KEY_Left};
    Combo desktop_snap_right{kModLogo, XKB_KEY_Right};
    Combo desktop_snap_up{kModLogo, XKB_KEY_Up};
    Combo desktop_snap_down{kModLogo, XKB_KEY_Down};
    Combo desktop_close_window{kModAlt, XKB_KEY_F4};
    Combo desktop_toggle_maximize{kModAlt, XKB_KEY_F10};
    Combo desktop_show_desktop{kModLogo, XKB_KEY_d};
    Combo desktop_minimize_all{kModLogo, XKB_KEY_m};
    Combo desktop_restore_all{kModLogo | kModShift, XKB_KEY_m};
    Combo cycle_windows{kModAlt, XKB_KEY_Tab};
    Combo cycle_windows_reverse{kModAlt | kModShift, XKB_KEY_Tab};
    Combo send_to_prev_screen{kModLogo | kModShift, XKB_KEY_Left};
    Combo send_to_next_screen{kModLogo | kModShift, XKB_KEY_Right};
    Combo workspace_prev{kModCtrl | kModAlt, XKB_KEY_Left};
    Combo workspace_next{kModCtrl | kModAlt, XKB_KEY_Right};
    unsigned workspace_switch_mods = kModLogo;                // + digit
    unsigned workspace_send_mods = kModLogo | kModShift;      // + digit
    std::vector<xkb_keysym_t> start_menu_syms{XKB_KEY_Super_L, XKB_KEY_Super_R};  // tap to open the start menu
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

  // Called from the SIGCHLD handler for every reaped child. If the lock
  // screen process dies while the session is still locked (crash, OOM kill),
  // it is respawned instead of the session being unlocked: fail closed. A
  // crash loop gives up after a few respawns (the session then stays locked
  // and can be recovered from another VT/ssh) rather than spinning forever.
  void on_child_exited(pid_t pid, int status);

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
  wlr_scene_tree* layer_topmost_ = nullptr;
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
  // wp-tearing-control-v1: lets a client (games/players via SDL/GLFW etc.)
  // hint that it wants async/tearing presentation -- queried per-frame by
  // output_frame() (output.cpp) to decide whether a fullscreen surface's
  // commit should request a real tearing page-flip. See RenderMode's own
  // doc comment (theme.hpp) for why this is automatic/fullscreen-only
  // rather than a user-selectable mode. No listener needed beyond
  // creation -- wlroots owns the whole protocol/hint-tracking internally.
  wlr_tearing_control_manager_v1* tearing_manager_ = nullptr;
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
  wlr_cursor_shape_manager_v1* cursor_shape_manager_ = nullptr;
  wl_listener request_set_shape_{};
  wlr_buffer* fallback_cursor_ = nullptr;  // built-in arrow, used when no cursor theme is installed
  int fallback_hotspot_x_ = 0, fallback_hotspot_y_ = 0;
  const char* cursor_name_ = nullptr;  // last xcursor name set by set_cursor_name()
  BorderColors border_colors_{{0.9f, 0.9f, 0.95f, 1.0f}, {0.9f, 0.9f, 0.95f, 1.0f}, {0.9f, 0.9f, 0.95f, 1.0f}};
  bool appearance_applied_ = false, appearance_dark_ = true;
  PowerConfig power_config_;
  wl_event_source* idle_timer_ = nullptr;
  std::chrono::steady_clock::time_point last_input_;
  bool displays_blanked_ = false;
  wlr_idle_inhibit_manager_v1* idle_inhibit_manager_ = nullptr;
  wl_listener new_idle_inhibitor_{};
  int idle_inhibitors_ = 0;
  void arm_idle_timer(long seconds_from_now);
  friend int idle_timer_cb(void* data);
  uint32_t next_view_id_ = 1;
  std::vector<View*> hidden_by_show_desktop_;
  std::vector<View*> cycle_order_;  // snapshot taken when an Alt+Tab cycle starts
  size_t cycle_index_ = 0;
  unsigned cycle_hold_mask_ = 0;
  wl_event_source* windows_idle_ = nullptr;

  enum class GrabMode { None, Move, Resize };
  GrabMode grab_mode_ = GrabMode::None;
  View* grab_view_ = nullptr;
  View* hover_view_ = nullptr;
  View* last_click_view_ = nullptr;
  uint32_t last_click_time_ = 0;
  double grab_cursor_x_ = 0, grab_cursor_y_ = 0;
  wlr_box grab_box_{};  // container x,y + content w,h when the grab began
  uint32_t grab_edges_ = 0;
  bool grab_unmaximize_pending_ = false;
  geom::SnapZone snap_pending_ = geom::SnapZone::None;  // zone under the cursor during a move
  wlr_scene_rect* snap_preview_ = nullptr;
  void update_snap_preview(View* view);
  void hide_snap_preview();
  OutputSettings output_settings_;
  void reconfigure_layer_surfaces(wlr_output* wlr_out);
  pid_t spawn_locker();
  std::vector<std::chrono::steady_clock::time_point> locker_respawns_;
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
  friend void server_new_xwayland_surface(wl_listener* listener, void* data);
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
