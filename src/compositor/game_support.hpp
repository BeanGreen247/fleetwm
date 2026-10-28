#pragma once

#include <wayland-server-core.h>

#include <sys/types.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include <wlr/version.h>
#if WLR_VERSION_NUM >= 0x001300
#include <wlr/types/wlr_content_type_v1.h>  // 0.18 installs the header without the protocol file it needs
#endif
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
}

namespace fleetwm {

class Server;
class View;

// What games and other latency-sensitive clients need from the compositor, kept apart from the rest of the server:
//   * pointer-constraints + relative-pointer: the mouse is locked or confined to the window and reports raw movement (first-person aiming, SDL relative mode);
//   * keyboard-shortcuts-inhibit: a fullscreen game gets Alt+Tab, Super and the other compositor shortcuts (Super+Pause hands them back);
//   * presentation-time, fractional-scale and explicit sync (linux-drm-syncobj) globals: the scene handles them once they exist.
class GameSupport {
 public:
  explicit GameSupport(Server* server) : server_(server) {}
  GameSupport(const GameSupport&) = delete;
  GameSupport& operator=(const GameSupport&) = delete;

  // Creates the globals. Call once the renderer, backend, scene and seat exist.
  void init();
  // Drops the listeners on the globals; wlroots asserts when they are still attached as the display is destroyed.
  void shutdown();

  // Pointer motion, before the cursor moves. Returns true when the cursor must stay where it is (a locked pointer); a confined pointer has dx, dy cut to
  // the allowed region. `unaccel` is the raw movement the relative-pointer event carries.
  bool filter_motion(double* dx, double* dy);
  void send_relative(uint32_t time_msec, double dx, double dy, double dx_unaccel, double dy_unaccel);
  // Where the surface under the pointer has its origin in layout coordinates, noted on every motion for the confinement maths.
  void note_pointer_surface(double origin_x, double origin_y) { origin_x_ = origin_x; origin_y_ = origin_y; }
  // The surface that has pointer focus changed: lock/confine it when it asked, release the old one.
  void pointer_focus_changed(wlr_surface* surface);
  bool pointer_locked() const { return active_ && active_->type == WLR_POINTER_CONSTRAINT_V1_LOCKED; }

  // Feral GameMode for every game, not only fullscreen ones: a window is a game when it is a Steam/Proton title (app id steam_app_*, SteamGameId in its
  // environment), a Wine program (.exe, WINEPREFIX), Lutris or gamescope, matches gamemode_apps in game.toml, or is fullscreen and not a video player or browser.
  // Such a window's program is registered with gamemoded (CPU governor, priorities, GPU clocks) and released when the window closes or stops being one.
  // Called when a window maps, goes fullscreen or leaves it. The call is made on a short-lived thread; no gamemoded, no effect.
  void evaluate_game(View* view);
  void view_gone(View* view);
  // The window's program was started for the dedicated card (__NV_PRIME_RENDER_OFFLOAD or DRI_PRIME in its environment). Cached per process.
  bool uses_prime_offload(View* view);
  // A launcher, store or configuration window (Battle.net, Epic, GOG, Heroic, Lutris, a game's own settings program): never a game.
  bool is_launcher(View* view);

  // Real-time scheduling for the compositor's main thread while a game runs (game.toml realtime), granted by rtkit. Dropped again after the last game.
  void set_realtime(bool on);

  // Keyboard focus changed: inhibitors of the focused surface become active, the others stop.
  void keyboard_focus_changed(wlr_surface* surface);
  // True while the focused window holds a shortcuts inhibitor: the compositor passes every key on.
  bool shortcuts_inhibited() const;
  // The escape hatch: the focused window's inhibitor is deactivated (until it is focused again).
  void release_shortcuts_inhibitor();

 private:
  struct Inhibitor {
    GameSupport* owner = nullptr;
    wlr_keyboard_shortcuts_inhibitor_v1* inhibitor = nullptr;
    wl_listener destroy{};
  };
  void set_active(wlr_pointer_constraint_v1* constraint);

  Server* server_;
  wlr_pointer_constraints_v1* constraints_ = nullptr;
  wlr_relative_pointer_manager_v1* relative_ = nullptr;
  wlr_keyboard_shortcuts_inhibit_manager_v1* inhibit_manager_ = nullptr;
  wl_listener new_constraint_{};
  wl_listener new_inhibitor_{};
  wlr_pointer_constraint_v1* active_ = nullptr;
  wl_listener active_destroy_{};
  std::vector<Inhibitor*> inhibitors_;
  std::map<View*, pid_t> gamemode_pids_;
  bool realtime_on_ = false;
  pid_t main_tid_ = 0;
#if WLR_VERSION_NUM >= 0x001300
  wlr_content_type_manager_v1* content_type_ = nullptr;
#endif
  bool tuxthrottle_on_ = false;  // we switched TuxThrottle's Game Mode on and owe it an off
  // The boost (TuxThrottle, real-time priority, fast desktop) is dropped a few seconds after the last game window goes, not at once: launchers and
  // old games open and close several windows in a row, and each switch ran TuxThrottle's helper scripts (fans, governor, GPU clocks) again.
  struct ProfileMemory {
    std::mutex mu;
    std::string before_game;  // the power profile to go back to
    std::string everyday;     // the last profile seen that was not "performance"
  };
  std::shared_ptr<ProfileMemory> profile_ = std::make_shared<ProfileMemory>();
  wl_event_source* end_timer_ = nullptr;
  bool boosted_ = false;
  void begin_boost(bool tuxthrottle);
  void end_boost();
  friend int game_end_timer_fire(void*);
  double origin_x_ = 0, origin_y_ = 0;
};

}  // namespace fleetwm
