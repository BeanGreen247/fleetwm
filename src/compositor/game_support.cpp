#include "game_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/region.h>
#include <wlr/version.h>
#if WLR_VERSION_NUM >= 0x001300
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#endif
}

#include <systemd/sd-bus.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cctype>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#include "server.hpp"
#include "tuxthrottle.hpp"
#include "view.hpp"

namespace fleetwm {

namespace {

// Programs that go fullscreen without being games: not worth a performance boost.
bool is_media_app(const char* app_id) {
  if (!app_id) return false;
  std::string id(app_id);
  for (char& c : id) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (const char* name : {"mpv", "vlc", "videolan", "celluloid", "totem", "smplayer", "kodi", "firefox", "chromium", "chrome", "brave", "vivaldi", "librewolf", "epiphany", "evince", "okular", "zathura"})
    if (id.find(name) != std::string::npos) return true;
  return false;
}

pid_t client_pid(View* view) {
#if FLEETWM_XWAYLAND
  if (view->kind == View::Kind::XWayland && view->xwayland_surface) return view->xwayland_surface->pid;
#endif
  if (view->kind == View::Kind::XdgToplevel && view->xdg_toplevel && view->xdg_toplevel->base && view->xdg_toplevel->base->resource) {
    pid_t pid = 0;
    wl_client_get_credentials(wl_resource_get_client(view->xdg_toplevel->base->resource), &pid, nullptr, nullptr);
    return pid;
  }
  return 0;
}

bool contains_ci(const std::string& hay, const std::string& needle) {
  if (needle.empty()) return false;
  auto lower = [](std::string x) {
    for (char& c : x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return x;
  };
  return lower(hay).find(lower(needle)) != std::string::npos;
}

// Does the program behind `pid` carry the marks of a Steam, Proton, Wine or Lutris game in its environment? (Same user, so readable.)
bool game_environment(pid_t pid) {
  std::ifstream f("/proc/" + std::to_string(pid) + "/environ", std::ios::binary);
  if (!f) return false;
  std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  size_t pos = 0;
  while (pos < all.size()) {
    size_t end = all.find('\0', pos);
    if (end == std::string::npos) end = all.size();
    const std::string var = all.substr(pos, end - pos);
    pos = end + 1;
    for (const char* key : {"SteamGameId=", "SteamAppId=", "STEAM_COMPAT_APP_ID=", "WINEPREFIX=", "LUTRIS_GAME_UUID=", "GAMEID="})
      if (var.rfind(key, 0) == 0) return true;
  }
  return false;
}

bool looks_like_game(View* view, pid_t pid, const GameConfig& gc, int content_type) {
  // wp_content_type_v1 values: 0 none, 1 photo, 2 video, 3 game. The program says so itself.
  if (content_type == 3) return true;
  if (content_type == 1 || content_type == 2) return false;
  const char* app = view->window_app_id();
  if (app && *app) {
    const std::string id = app;
    if (contains_ci(id, "steam_app_") || contains_ci(id, ".exe") || contains_ci(id, "gamescope")) return true;
    for (const std::string& extra : gc.gamemode_apps)
      if (contains_ci(id, extra)) return true;
  }
  if (view->fullscreen && !is_media_app(app)) return true;
  return game_environment(pid);
}

// com.feralinteractive.GameMode on the session bus, from a short-lived thread so a missing or slow daemon never holds up the compositor.
void gamemode_call(const char* method, pid_t target) {
  std::thread([method, target] {
    sd_bus* bus = nullptr;
    if (sd_bus_open_user(&bus) < 0) return;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_call_method(bus, "com.feralinteractive.GameMode", "/com/feralinteractive/GameMode", "com.feralinteractive.GameMode", method, &err, nullptr, "ii",
                       static_cast<int>(getpid()), static_cast<int>(target));
    sd_bus_error_free(&err);
    sd_bus_unref(bus);
  }).detach();
}

}  // namespace

void GameSupport::view_gone(View* view) {
  auto it = gamemode_pids_.find(view);
  if (it == gamemode_pids_.end()) return;
  gamemode_call("UnregisterGameByPID", it->second);
  gamemode_pids_.erase(it);
  // The last game is gone: TuxThrottle goes back to its everyday profile.
  if (gamemode_pids_.empty()) set_realtime(false);
  if (gamemode_pids_.empty() && tuxthrottle_on_) {
    tuxthrottle_on_ = false;
    tuxthrottle_game_mode("off");
  }
}

// A headless or nested compositor (the test rigs, PGO training, a window inside another desktop) has no screen of its own to speed up: it must never switch the
// machine's GameMode, TuxThrottle or scheduling. FLEETWM_GAMEMODE_TEST=1 lets the game-support test rig do it against a private gamemoded.
bool boosts_allowed() {
  static const bool allowed = [] {
    if (std::getenv("FLEETWM_GAMEMODE_TEST")) return true;
    const char* backends = std::getenv("WLR_BACKENDS");
    return !backends || !*backends || std::strstr(backends, "drm") != nullptr;
  }();
  return allowed;
}

void GameSupport::evaluate_game(View* view) {
  if (!boosts_allowed()) return;
  const GameConfig& gc = server_->game_config();
  const pid_t pid = gc.gamemode ? client_pid(view) : 0;
  int content_type = 0;
#if WLR_VERSION_NUM >= 0x001300
  if (content_type_ && view->surface()) content_type = static_cast<int>(wlr_surface_get_content_type_v1(content_type_, view->surface()));
#endif
  const bool game = pid > 0 && looks_like_game(view, pid, gc, content_type);
  auto it = gamemode_pids_.find(view);
  if (game && it == gamemode_pids_.end()) {
    gamemode_pids_[view] = pid;
    gamemode_call("RegisterGameByPID", pid);
    if (gamemode_pids_.size() == 1 && gc.realtime) set_realtime(true);
    // The first game: TuxThrottle's Game Mode (performance profile, fans, GPU clocks) on a gaming laptop that has it.
    if (!tuxthrottle_on_ && gc.tuxthrottle && tuxthrottle_installed()) {
      tuxthrottle_on_ = true;
      tuxthrottle_game_mode("on");
    }
  } else if (!game && it != gamemode_pids_.end()) {
    view_gone(view);
  }
}

void GameSupport::set_realtime(bool on) {
  if (on == realtime_on_) return;
  realtime_on_ = on;
  if (!on) {
    sched_param sp{};
    sched_setscheduler(0, SCHED_OTHER, &sp);  // the calling (main) thread: dropping to the normal class needs no privilege
    return;
  }
  // rtkit insists on a CPU-time ceiling for real-time threads: a runaway one is killed by the kernel instead of freezing the machine.
  rlimit rl{200000, 200000};  // microseconds without sleeping
  setrlimit(RLIMIT_RTTIME, &rl);
  const unsigned long long tid = static_cast<unsigned long long>(main_tid_);
  std::thread([tid] {
    sd_bus* bus = nullptr;
    if (sd_bus_open_system(&bus) < 0) return;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    const int r = sd_bus_call_method(bus, "org.freedesktop.RealtimeKit1", "/org/freedesktop/RealtimeKit1", "org.freedesktop.RealtimeKit1", "MakeThreadRealtime", &err,
                                     nullptr, "tu", static_cast<uint64_t>(tid), 10u);
    if (r < 0) std::fprintf(stderr, "fleetwm: real-time priority refused by rtkit: %s\n", err.message ? err.message : "no rtkit?");
    sd_bus_error_free(&err);
    sd_bus_unref(bus);
  }).detach();
}

void GameSupport::init() {
  main_tid_ = static_cast<pid_t>(syscall(SYS_gettid));
#if WLR_VERSION_NUM >= 0x001300
  content_type_ = wlr_content_type_manager_v1_create(server_->display(), 1);
#endif
  constraints_ = wlr_pointer_constraints_v1_create(server_->display());
  new_constraint_.notify = [](wl_listener* listener, void* data) {
    GameSupport* self = wl_container_of(listener, self, new_constraint_);
    auto* constraint = static_cast<wlr_pointer_constraint_v1*>(data);
    // A constraint made for the surface that already has the pointer takes hold at once; others wait for the pointer to arrive.
    if (constraint->surface == self->server_->seat()->pointer_state.focused_surface) self->set_active(constraint);
  };
  wl_signal_add(&constraints_->events.new_constraint, &new_constraint_);

  relative_ = wlr_relative_pointer_manager_v1_create(server_->display());

  inhibit_manager_ = wlr_keyboard_shortcuts_inhibit_v1_create(server_->display());
  new_inhibitor_.notify = [](wl_listener* listener, void* data) {
    GameSupport* self = wl_container_of(listener, self, new_inhibitor_);
    auto* inhibitor = new Inhibitor;
    inhibitor->owner = self;
    inhibitor->inhibitor = static_cast<wlr_keyboard_shortcuts_inhibitor_v1*>(data);
    inhibitor->destroy.notify = [](wl_listener* l, void*) {
      Inhibitor* in = wl_container_of(l, in, destroy);
      auto& v = in->owner->inhibitors_;
      v.erase(std::remove(v.begin(), v.end(), in), v.end());
      wl_list_remove(&in->destroy.link);
      delete in;
    };
    wl_signal_add(&inhibitor->inhibitor->events.destroy, &inhibitor->destroy);
    self->inhibitors_.push_back(inhibitor);
    if (inhibitor->inhibitor->surface == self->server_->seat()->keyboard_state.focused_surface)
      wlr_keyboard_shortcuts_inhibitor_v1_activate(inhibitor->inhibitor);
  };
  wl_signal_add(&inhibit_manager_->events.new_inhibitor, &new_inhibitor_);

  // Frame timing for Vulkan/DXVK pacing, per-surface scale for HiDPI games, and fences instead of implicit sync: the scene sends the feedback, the scale
  // and the release points itself once these globals exist.
#if WLR_VERSION_NUM >= 0x001300
  wlr_presentation_create(server_->display(), server_->backend(), 2);
#else
  wlr_presentation_create(server_->display(), server_->backend());
#endif
  wlr_fractional_scale_manager_v1_create(server_->display(), 1);
#if WLR_VERSION_NUM >= 0x001300
  // Explicit sync needs the renderer and the backend to take timelines (a DRM device with syncobj support); otherwise the global is not offered.
  const int drm_fd = wlr_renderer_get_drm_fd(server_->renderer());
  if (drm_fd >= 0 && server_->renderer()->features.timeline && server_->backend()->features.timeline)
    wlr_linux_drm_syncobj_manager_v1_create(server_->display(), 1, drm_fd);
#endif
}

void GameSupport::shutdown() {
  if (!constraints_) return;
  wl_list_remove(&new_constraint_.link);
  wl_list_remove(&new_inhibitor_.link);
  constraints_ = nullptr;
}

bool GameSupport::filter_motion(double* dx, double* dy) {
  if (!active_) return false;
  if (active_->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) return true;
  // Confined: the pointer may move inside the region only (surface-local coordinates).
  wlr_cursor* cursor = server_->cursor();
  const double sx = cursor->x - origin_x_, sy = cursor->y - origin_y_;
  double nx = sx, ny = sy;
  if (!wlr_region_confine(&active_->region, sx, sy, sx + *dx, sy + *dy, &nx, &ny)) {
    *dx = *dy = 0;
    return false;
  }
  *dx = nx - sx;
  *dy = ny - sy;
  return false;
}

void GameSupport::send_relative(uint32_t time_msec, double dx, double dy, double dx_unaccel, double dy_unaccel) {
  if (relative_)
    wlr_relative_pointer_manager_v1_send_relative_motion(relative_, server_->seat(), static_cast<uint64_t>(time_msec) * 1000, dx, dy, dx_unaccel, dy_unaccel);
}

void GameSupport::set_active(wlr_pointer_constraint_v1* constraint) {
  if (constraint == active_) return;
  if (active_) {
    wl_list_remove(&active_destroy_.link);
    wlr_pointer_constraint_v1* old = active_;
    active_ = nullptr;
    // A locked pointer's hint says where the cursor should reappear (a game's own crosshair position); honour it.
    if (old->type == WLR_POINTER_CONSTRAINT_V1_LOCKED && old->current.cursor_hint.enabled)
      wlr_cursor_warp(server_->cursor(), nullptr, origin_x_ + old->current.cursor_hint.x, origin_y_ + old->current.cursor_hint.y);
    wlr_pointer_constraint_v1_send_deactivated(old);
  }
  active_ = constraint;
  if (active_) {
    active_destroy_.notify = [](wl_listener* listener, void*) {
      GameSupport* self = wl_container_of(listener, self, active_destroy_);
      wl_list_remove(&self->active_destroy_.link);
      self->active_ = nullptr;
    };
    wl_signal_add(&active_->events.destroy, &active_destroy_);
    wlr_pointer_constraint_v1_send_activated(active_);
  }
}

void GameSupport::pointer_focus_changed(wlr_surface* surface) {
  if (!constraints_) return;
  wlr_pointer_constraint_v1* wanted = surface ? wlr_pointer_constraints_v1_constraint_for_surface(constraints_, surface, server_->seat()) : nullptr;
  set_active(wanted);
}

void GameSupport::keyboard_focus_changed(wlr_surface* surface) {
  for (Inhibitor* in : inhibitors_) {
    if (in->inhibitor->surface == surface) wlr_keyboard_shortcuts_inhibitor_v1_activate(in->inhibitor);
    else wlr_keyboard_shortcuts_inhibitor_v1_deactivate(in->inhibitor);
  }
}

bool GameSupport::shortcuts_inhibited() const {
  wlr_surface* focused = server_->seat()->keyboard_state.focused_surface;
  if (!focused) return false;
  return std::any_of(inhibitors_.begin(), inhibitors_.end(), [focused](const Inhibitor* in) { return in->inhibitor->active && in->inhibitor->surface == focused; });
}

void GameSupport::release_shortcuts_inhibitor() {
  wlr_surface* focused = server_->seat()->keyboard_state.focused_surface;
  for (Inhibitor* in : inhibitors_)
    if (in->inhibitor->surface == focused) wlr_keyboard_shortcuts_inhibitor_v1_deactivate(in->inhibitor);
}

}  // namespace fleetwm
