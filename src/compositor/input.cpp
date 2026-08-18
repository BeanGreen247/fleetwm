#include "input.hpp"

#include <unistd.h>
#include <wayland-server-core.h>
#include <xkbcommon/xkbcommon.h>

extern "C" {
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
}

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "output.hpp"
#include "server.hpp"
#include "view.hpp"

namespace fleetwm {

namespace {

// The actual key each Alt+<key>/Alt+Shift+<key> bind uses is remappable
// via keybinds.toml (see keybinds_config.hpp) -- Keyboard::handle_keybind()
// below reads the resolved xkb_keysym_t values off server->keybinds()
// live, rather than fixed constexpr constants like this file used to
// define here. Only the modifier itself (always Alt, Shift for the
// combined ones) stays fixed -- see the class doc comment in
// keybinds_config.hpp for why. Uppercase key names in keybinds.toml mean
// "Shift resolves into the keysym itself" (e.g. "Q" is Alt+Shift+Q),
// same convention xkb itself uses; kPromoteKey's old special case
// (sharing a physical key with spawn-terminal, distinguished only by an
// explicit shift_held check since Return has no separate shifted keysym)
// still works the same way below, just off server->keybinds().terminal
// instead of a fixed constant.
constexpr const char* kLauncherCommand = "fleetwm-launcher";

// Alt+Shift+<screenshot>: region-select screenshot, copied to the
// clipboard with a desktop notification -- same grim+slurp+wl-copy+
// notify-send combo (and $mod+Shift+s binding) the project's own sway
// config used (github.com/BeanGreen247/sway-setup-script), ported to
// this compositor's Alt-based convention. Runs via spawn_shell() (not
// spawn()) since it needs a pipe, not a bare argv-less binary.
constexpr const char* kScreenshotCommand =
    "grim -g \"$(slurp)\" - | wl-copy && notify-send 'Screenshot' 'Copied to clipboard'";

// Finds the View owning the seat's currently keyboard-focused surface, if
// any -- the seat only tracks a wlr_surface*, not the owning View, so
// keybinds that need "the focused window" (e.g. kCloseWindowKey) look it
// up by scanning views the same way focus_view()'s callers already assume
// is cheap (Phase 0 view counts are small).
View* focused_view(Server* server) {
  wlr_surface* focused_surface = server->seat()->keyboard_state.focused_surface;
  if (!focused_surface) {
    return nullptr;
  }
  for (const std::unique_ptr<View>& view : server->views) {
    if (view->surface() == focused_surface) {
      return view.get();
    }
  }
  return nullptr;
}

// Approximate on-screen box of `view`'s container_tree, in output-layout
// coordinates. container_tree->node.x/y are relative to its immediate
// parent (a wlr_scene_node_t field, per wlroots), but that parent is
// always one of Server's layer_* trees, which are all created at (0,0)
// under scene_->tree and never repositioned -- so in practice these are
// already absolute output-layout coordinates, the same assumption
// tile_view()/View::set_fullscreen() (output.cpp/view.cpp) already make
// when they call wlr_scene_node_set_position() with raw output-box
// values. Size is the client's last-committed content geometry plus the
// view's current border thickness on each side, recomputed the same way
// resize_border() (view.cpp) does -- not tracked as a separate field
// anywhere.
wlr_box view_box(View* view) {
  wlr_box box{};
  box.x = view->container_tree->node.x;
  box.y = view->container_tree->node.y;
  wlr_box geo{};
  if (view->kind == View::Kind::XdgToplevel && view->xdg_toplevel) {
    wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);
  }
  int thickness = view->border_thickness();
  box.width = std::max(1, geo.width) + 2 * thickness;
  box.height = std::max(1, geo.height) + 2 * thickness;
  return box;
}

enum class Direction { Left, Right, Up, Down };

// Finds the nearest *visible* view in `dir` from `current`'s screen
// position -- a real spatial search (center-point distance, weighted
// against perpendicular misalignment), not a stacking-order cycle.
// Standard "focus in direction" heuristic, same shape as i3/sway's own
// direction-focus tools: candidates strictly on the requested side score
// by (distance along that axis) + 2x(misalignment on the other axis), so
// a window slightly farther but well-aligned beats one closer but
// off-axis. Returns nullptr if `current` is null or nothing qualifies
// (e.g. already at the edge in that direction).
View* find_view_in_direction(Server* server, View* current, Direction dir) {
  if (current == nullptr) {
    return nullptr;
  }
  wlr_box current_box = view_box(current);
  int fx = current_box.x + current_box.width / 2;
  int fy = current_box.y + current_box.height / 2;

  View* best = nullptr;
  long best_score = 0;
  for (const std::unique_ptr<View>& v : server->views) {
    if (v.get() == current || !v->container_tree->node.enabled) {
      continue;  // enabled mirrors the same visibility check
                 // Output::switch_workspace() uses -- an invisible
                 // (different-workspace, non-pinned) view is never a
                 // sensible focus target.
    }
    wlr_box box = view_box(v.get());
    int dx = (box.x + box.width / 2) - fx;
    int dy = (box.y + box.height / 2) - fy;

    long primary;
    long secondary;
    switch (dir) {
      case Direction::Left:
        if (dx >= 0) continue;
        primary = -dx;
        secondary = std::abs(dy);
        break;
      case Direction::Right:
        if (dx <= 0) continue;
        primary = dx;
        secondary = std::abs(dy);
        break;
      case Direction::Up:
        if (dy >= 0) continue;
        primary = -dy;
        secondary = std::abs(dx);
        break;
      case Direction::Down:
      default:
        if (dy <= 0) continue;
        primary = dy;
        secondary = std::abs(dx);
        break;
    }
    long score = primary + secondary * 2;
    if (best == nullptr || score < best_score) {
      best = v.get();
      best_score = score;
    }
  }
  return best;
}

void spawn(const char* cmd) {
  pid_t pid = fork();
  if (pid < 0) {
    std::fprintf(stderr, "fleetwm: fork for '%s' spawn failed: %s\n", cmd, std::strerror(errno));
    return;
  }
  if (pid == 0) {
    execlp(cmd, cmd, nullptr);
    // execlp only returns on failure -- log why before the child dies, since
    // this failure would otherwise be completely silent.
    std::fprintf(stderr, "fleetwm: failed to exec '%s': %s\n", cmd, std::strerror(errno));
    _exit(1);
  }
}

// Same fork+exec shape as spawn() above, but for a shell command line
// that needs pipes/subshells (e.g. kScreenshotCommand's `grim ... | wl-
// copy`) -- spawn()'s execlp(cmd, cmd, nullptr) can only run a bare
// binary with no arguments at all. Same precedent as launcher_window.cpp's
// launch_command() routing a typed command through `/bin/sh -c`.
void spawn_shell(const char* shell_cmd) {
  pid_t pid = fork();
  if (pid < 0) {
    std::fprintf(stderr, "fleetwm: fork for '%s' spawn failed: %s\n", shell_cmd,
                 std::strerror(errno));
    return;
  }
  if (pid == 0) {
    execlp("/bin/sh", "/bin/sh", "-c", shell_cmd, nullptr);
    std::fprintf(stderr, "fleetwm: failed to exec '%s': %s\n", shell_cmd, std::strerror(errno));
    _exit(1);
  }
}

void keyboard_modifiers(wl_listener* listener, void*) {
  Keyboard* keyboard = wl_container_of(listener, keyboard, modifiers);
  wlr_seat_set_keyboard(keyboard->server->seat(), keyboard->wlr_keyboard_ptr);
  wlr_seat_keyboard_notify_modifiers(keyboard->server->seat(),
                                      &keyboard->wlr_keyboard_ptr->modifiers);
}

void keyboard_key(wl_listener* listener, void* data) {
  Keyboard* keyboard = wl_container_of(listener, keyboard, key);
  auto* event = static_cast<wlr_keyboard_key_event*>(data);

  uint32_t keycode = event->keycode + 8;  // xkbcommon uses evdev + 8
  const xkb_keysym_t* syms;
  int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard_ptr->xkb_state, keycode, &syms);

  bool alt_held = (wlr_keyboard_get_modifiers(keyboard->wlr_keyboard_ptr) & WLR_MODIFIER_ALT) != 0;
  bool handled = false;

  // While locked, no global Alt+<key> keybind (spawn terminal, launcher,
  // close window, etc.) may fire -- otherwise Alt+Return would spawn a
  // terminal straight through the lock screen. Every other key still
  // just flows to wlr_seat_keyboard_notify_key() below, which routes to
  // whatever surface currently holds seat keyboard focus -- safe here
  // because fleetwm-locker's lock surface is KEYBOARD_MODE_EXCLUSIVE and
  // already owns that focus for as long as the session is locked (see
  // Server::is_locked()'s doc comment, server.hpp).
  if (!keyboard->server->is_locked() && alt_held &&
      event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    for (int i = 0; i < nsyms; ++i) {
      if (keyboard->handle_keybind(syms[i])) {
        handled = true;
        break;
      }
    }
  }

  if (!handled) {
    wlr_seat_set_keyboard(keyboard->server->seat(), keyboard->wlr_keyboard_ptr);
    wlr_seat_keyboard_notify_key(keyboard->server->seat(), event->time_msec, event->keycode,
                                  event->state);
  }
}

void keyboard_destroy(wl_listener* listener, void*) {
  Keyboard* keyboard = wl_container_of(listener, keyboard, destroy);
  delete keyboard;
}

}  // namespace

Keyboard::Keyboard(Server* server_, wlr_keyboard* wlr_keyboard_ptr_)
    : server(server_), wlr_keyboard_ptr(wlr_keyboard_ptr_) {
  xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  xkb_keymap* keymap =
      xkb_keymap_new_from_names(context, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);

  wlr_keyboard_set_keymap(wlr_keyboard_ptr, keymap);
  xkb_keymap_unref(keymap);
  xkb_context_unref(context);
  wlr_keyboard_set_repeat_info(wlr_keyboard_ptr, 25, 600);

  modifiers.notify = keyboard_modifiers;
  wl_signal_add(&wlr_keyboard_ptr->events.modifiers, &modifiers);

  key.notify = keyboard_key;
  wl_signal_add(&wlr_keyboard_ptr->events.key, &key);

  destroy.notify = keyboard_destroy;
  wl_signal_add(&wlr_keyboard_ptr->base.events.destroy, &destroy);

  server->notify_keyboard_added();
}

Keyboard::~Keyboard() {
  wl_list_remove(&modifiers.link);
  wl_list_remove(&key.link);
  wl_list_remove(&destroy.link);
  server->notify_keyboard_removed();
}

bool Keyboard::handle_keybind(xkb_keysym_t sym) {
  bool shift_held = (wlr_keyboard_get_modifiers(wlr_keyboard_ptr) & WLR_MODIFIER_SHIFT) != 0;
  const Server::ResolvedKeybinds& binds = server->keybinds();

  if (sym == binds.terminal) {
    if (shift_held) {
      if (View* view = focused_view(server)) {
        // Promote to master: splice to front of server->views the same
        // way focus_view() already does for topmost-on-focus, then
        // re-tile -- master is defined as "first in the tiled set",
        // which relayout() derives from this same list.
        server->focus_view(view);
        if (view->output) {
          view->output->relayout();
        }
      }
      return true;
    }
    // Read live rather than cached-at-startup: settings' Default Apps
    // tab writes default_apps.toml, and server picks it up via the same
    // inotify watch as theme.toml (see server.cpp's
    // server_theme_watch_readable), so a change here takes effect on
    // the next Enter press with no restart needed.
    spawn(server->default_apps_config().terminal_command.c_str());
    return true;
  }
  if (sym == binds.launcher) {
    spawn(kLauncherCommand);
    return true;
  }
  if (sym == binds.close_window) {
    if (View* view = focused_view(server)) {
      view->close();
    }
    return true;
  }
  if (sym == binds.toggle_pin) {
    if (View* view = focused_view(server)) {
      view->set_pinned(!view->pinned);
    }
    return true;
  }
  if (sym == binds.lock) {
    server->request_lock();
    return true;
  }
  if (sym == binds.screenshot) {
    spawn_shell(kScreenshotCommand);
    return true;
  }
  if (sym == binds.toggle_float) {
    if (View* view = focused_view(server)) {
      view->set_floating(!view->floating);
      if (view->output) {
        view->output->relayout();
      }
    }
    return true;
  }
  if (sym == binds.focus_left || sym == binds.focus_right || sym == binds.focus_up ||
      sym == binds.focus_down) {
    Direction dir = sym == binds.focus_left    ? Direction::Left
                     : sym == binds.focus_right ? Direction::Right
                     : sym == binds.focus_up    ? Direction::Up
                                                 : Direction::Down;
    if (View* target = find_view_in_direction(server, focused_view(server), dir)) {
      server->focus_view(target);
    }
    return true;
  }
  if (sym == binds.quit) {
    wl_display_terminate(server->display());
    return true;
  }
  if (sym == binds.debug_overlay) {
    server->toggle_debug_overlay();
    return true;
  }
  return false;
}

}  // namespace fleetwm
