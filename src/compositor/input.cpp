#include "child_signals.hpp"
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
#include <filesystem>
#include <string>
#include <filesystem>
#include <string>
#include <memory>
#include <vector>

#include "output.hpp"
#include "paths_config.h"
#include "paths_config.h"
#include "server.hpp"
#include "terminal_launch.hpp"
#include "terminal_launch.hpp"
#include "view.hpp"

extern char** environ;

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
constexpr const char* kStartMenuCommand = "fleetwm-launcher --start-menu";  // toggles
constexpr const char* kShortcutsCommand = "fleetwm-shortcuts";  // toggles: a second launch closes the first

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
  if (view->is_window()) {
    geo = view->content_geometry();
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
    fleetwm::reset_signals_for_exec();
    execlp(cmd, cmd, nullptr);
    // execlp only returns on failure -- log why before the child dies, since
    // this failure would otherwise be completely silent.
    std::fprintf(stderr, "fleetwm: failed to exec '%s': %s\n", cmd, std::strerror(errno));
    _exit(1);
  }
}

// Same fork+exec shape as spawn() above, but strips LD_PRELOAD/MALLOC_CONF
// from the child's environment first -- for the user's own terminal
// specifically, not fleetwm's own clients (which spawn() above still hands
// those vars to unmodified, e.g. kLauncherCommand). Those two vars are set
// process-wide by the greeter (session.cpp's build_env(), preloading
// jemalloc for fleetwm's own long-running clients' RSS, see that
// file's comment for the full rationale) and, being plain environment
// variables, cascade into every child a plain execlp() spawns -- including
// this terminal and therefore every command the user types inside it.
// Confirmed on real hardware: `sudo apt update` from a fleetwm-launched
// terminal printed "ld.so: object 'libjemalloc.so.2' from LD_PRELOAD
// cannot be preloaded ... ignored" on every invocation, because sudo's
// setuid re-exec applies glibc's AT_SECURE restrictions to LD_PRELOAD
// resolution -- alarming, confusing noise for something the user never
// asked to run under a tuned allocator. A user's shell and whatever they
// run in it should behave like a stock system shell, not silently inherit
// fleetwm's own internal memory tuning.
void spawn_terminal(const char* command) {
  const std::string sysconf = FLEETWM_SYSCONF_DIR;
  const char* home = std::getenv("HOME");
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  const std::string user_ini =
      (xdg && *xdg ? std::string(xdg) : std::string(home ? home : "") + "/.config") + "/foot/foot.ini";
  std::error_code ec;
  const std::vector<std::string> args = terminal_argv(
      command, sysconf, std::filesystem::exists(user_ini, ec), std::filesystem::exists(sysconf + "/foot.ini", ec));
  const char* cmd = args[0].c_str();
  std::vector<char*> argv;
  for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);
  pid_t pid = fork();
  if (pid < 0) {
    std::fprintf(stderr, "fleetwm: fork for '%s' spawn failed: %s\n", cmd, std::strerror(errno));
    return;
  }
  if (pid == 0) {
    fleetwm::reset_signals_for_exec();
    std::vector<char*> env;
    for (char** e = environ; *e != nullptr; ++e) {
      if (std::strncmp(*e, "LD_PRELOAD=", 11) != 0 && std::strncmp(*e, "MALLOC_CONF=", 12) != 0) {
        env.push_back(*e);
      }
    }
    env.push_back(nullptr);
    execvpe(cmd, argv.data(), env.data());
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
    fleetwm::reset_signals_for_exec();
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
  // Letting go of the Alt in Alt+Tab settles the window cycle on the current window.
  Server* server = keyboard->server;
  if (server->cycling() &&
      (wlr_keyboard_get_modifiers(keyboard->wlr_keyboard_ptr) & server->cycle_hold_mask()) != server->cycle_hold_mask()) {
    server->end_window_cycle();
  }
}

void keyboard_key(wl_listener* listener, void* data) {
  Keyboard* keyboard = wl_container_of(listener, keyboard, key);
  auto* event = static_cast<wlr_keyboard_key_event*>(data);

  keyboard->server->note_activity();
  uint32_t keycode = event->keycode + 8;  // xkbcommon uses evdev + 8
  const xkb_keysym_t* syms;
  int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard_ptr->xkb_state, keycode, &syms);

  // The Tiling layout's held modifier: Alt by default, Super when Settings -> Keyboard says so.
  bool alt_held = (wlr_keyboard_get_modifiers(keyboard->wlr_keyboard_ptr) & keyboard->server->keybinds().tiling_mod) != 0;
  bool handled = false;

  // Desktop layout: tapping Super (Windows/Meta) alone opens the start menu; a
  // second tap closes it (the launcher toggles itself). Any other key pressed in
  // between makes it a modifier use, not a tap.
  {
    bool is_super = false;  // the configurable start-menu key (Super by default)
    for (int i = 0; i < nsyms; ++i)
      for (xkb_keysym_t start_key : keyboard->server->keybinds().start_menu_syms)
        if (syms[i] == start_key) is_super = true;
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
      keyboard->super_tap = is_super;
    } else if (is_super && keyboard->super_tap) {
      keyboard->super_tap = false;
      if (keyboard->server->desktop_layout() && !keyboard->server->is_locked()) {
        spawn_shell(kStartMenuCommand);
      }
    }
  }

  // Alt+Shift (pressed together, nothing else in between) switches the keyboard layout when
  // letting go of one of them, like Windows. Any other key pressed meanwhile cancels it. The
  // keys are tracked from the key events themselves: the modifier state wlroots reports lags
  // one event behind inside this handler.
  {
    const KeyboardConfig& kc = keyboard->server->keyboard_config();
    bool is_alt = false, is_shift = false;
    for (int i = 0; i < nsyms; ++i) {
      if (syms[i] == XKB_KEY_Alt_L || syms[i] == XKB_KEY_Alt_R) is_alt = true;
      if (syms[i] == XKB_KEY_Shift_L || syms[i] == XKB_KEY_Shift_R) is_shift = true;
    }
    const bool pressed = event->state == WL_KEYBOARD_KEY_STATE_PRESSED;
    if (pressed) {
      if (is_alt) keyboard->alt_down = true;
      if (is_shift) keyboard->shift_down = true;
      if (!is_alt && !is_shift) keyboard->layout_chord = false;
      else if (keyboard->alt_down && keyboard->shift_down) keyboard->layout_chord = true;
    } else if (is_alt || is_shift) {
      const bool fire = keyboard->layout_chord;
      if (is_alt) keyboard->alt_down = false;
      if (is_shift) keyboard->shift_down = false;
      keyboard->layout_chord = false;
      if (fire && !keyboard->server->is_locked() && kc.layouts.size() > 1 && kc.switch_keys != LayoutSwitchKeys::SuperSpace)
        keyboard->server->step_layout(1);
    }
  }

  // Combo shortcuts (written as "ctrl+alt+t" in keybinds.toml). Everywhere: the
  // shortcuts window, Alt+Tab window cycling, sending a window to another screen, and
  // workspace switching. Desktop layout only: terminal, default apps, the overlay and
  // the Windows-style snap keys. They are separate from the Alt-based Tiling shortcuts,
  // which are all off in the Desktop layout.
  {
    wlr_keyboard* kb = keyboard->wlr_keyboard_ptr;
    const unsigned mods = wlr_keyboard_get_modifiers(kb);
    Server* server = keyboard->server;
    const Server::ResolvedKeybinds& binds = server->keybinds();
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED && !server->is_locked()) {
      // The key with no modifiers applied ("1", not "!"), so Shift combos match by key.
      xkb_keysym_t sym0 = XKB_KEY_NoSymbol;
      const xkb_layout_index_t layout = xkb_state_key_get_layout(kb->xkb_state, keycode);
      const xkb_keysym_t* level0 = nullptr;
      if (xkb_keymap_key_get_syms_by_level(kb->keymap, keycode, layout, 0, &level0) > 0) sym0 = level0[0];
      auto is = [&](const Server::ResolvedKeybinds::Combo& c, xkb_keysym_t sym) {
        if (!combo_mods_match(mods, c.mods)) return false;
        const xkb_keysym_t want = xkb_keysym_to_lower(c.sym);
        if (xkb_keysym_to_lower(sym) == want || xkb_keysym_to_lower(sym0) == want) return true;
        return c.sym == XKB_KEY_Tab && sym == XKB_KEY_ISO_Left_Tab;  // Shift+Tab reports a different key
      };
      const bool desktop = server->desktop_layout();
      const int digit = sym0 >= XKB_KEY_1 && sym0 <= XKB_KEY_9 ? static_cast<int>(sym0 - XKB_KEY_1)
                        : sym0 == XKB_KEY_0                   ? 9
                                                              : -1;
      for (int i = 0; i < nsyms && !handled; ++i) {
        const xkb_keysym_t sym = syms[i];
        if (is(binds.shortcuts_help, sym)) {
          spawn(kShortcutsCommand);
          handled = true;
        } else if (server->keyboard_config().switch_keys != LayoutSwitchKeys::AltShift &&
                   (is(binds.keyboard_next_layout, sym) || is(binds.keyboard_prev_layout, sym))) {
          server->step_layout(is(binds.keyboard_prev_layout, sym) ? -1 : 1);
          handled = true;
        } else if (is(binds.cycle_windows, sym) || is(binds.cycle_windows_reverse, sym)) {
          const bool back = is(binds.cycle_windows_reverse, sym);
          const unsigned hold = (back ? binds.cycle_windows_reverse.mods : binds.cycle_windows.mods) & ~kModShift;
          server->cycle_windows(back, hold);
          handled = true;
        } else if (is(binds.send_to_prev_screen, sym) || is(binds.send_to_next_screen, sym)) {
          if (View* view = server->focused_view_for_actions())
            server->move_view_to_screen(view, is(binds.send_to_prev_screen, sym) ? -1 : 1);
          handled = true;
        } else if (is(binds.workspace_prev, sym) || is(binds.workspace_next, sym)) {
          server->switch_workspace_relative(is(binds.workspace_prev, sym) ? -1 : 1);
          handled = true;
        } else if (digit >= 0 && combo_mods_match(mods, binds.workspace_switch_mods)) {
          server->switch_workspace(digit);
          handled = true;
        } else if (digit >= 0 && combo_mods_match(mods, binds.workspace_send_mods)) {
          if (View* view = server->focused_view_for_actions()) server->move_view_to_workspace(view, digit);
          handled = true;
        } else if (desktop && is(binds.desktop_close_window, sym)) {
          if (View* view = server->focused_view_for_actions()) view->close();
          handled = true;
        } else if (desktop && is(binds.desktop_toggle_maximize, sym)) {
          if (View* view = server->focused_view_for_actions(); view && !view->fullscreen)
            view->set_maximized(!view->maximized);
          handled = true;
        } else if (desktop && is(binds.desktop_show_desktop, sym)) {
          server->show_desktop_toggle();
          handled = true;
        } else if (desktop && is(binds.desktop_minimize_all, sym)) {
          server->minimize_all();
          handled = true;
        } else if (desktop && is(binds.desktop_restore_all, sym)) {
          server->restore_all();
          handled = true;
        } else if (desktop && is(binds.desktop_snap_left, sym)) {
          server->snap_step_focused(geom::Direction::Left);
          handled = true;
        } else if (desktop && is(binds.desktop_snap_right, sym)) {
          server->snap_step_focused(geom::Direction::Right);
          handled = true;
        } else if (desktop && is(binds.desktop_snap_up, sym)) {
          server->snap_step_focused(geom::Direction::Up);
          handled = true;
        } else if (desktop && is(binds.desktop_snap_down, sym)) {
          server->snap_step_focused(geom::Direction::Down);
          handled = true;
        } else if (desktop && is(binds.desktop_debug_overlay, sym)) {
          server->toggle_debug_overlay();
          handled = true;
        } else if (desktop && is(binds.desktop_terminal, sym)) {
          spawn_terminal(server->default_apps_config().terminal_command.c_str());
          handled = true;
        } else if (desktop) {
          const char* cmd = is(binds.desktop_task_manager, sym)   ? "fleetwm-taskmgr"
                            : is(binds.desktop_browser, sym)      ? "fleetwm-launcher --default browser"
                            : is(binds.desktop_file_manager, sym) ? "fleetwm-launcher --default files"
                            : is(binds.desktop_text_editor, sym)  ? "fleetwm-launcher --default editor"
                                                                  : nullptr;
          if (cmd) {
            spawn_shell(cmd);
            handled = true;
          }
        }
      }
    }
  }

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

Keyboard::Keyboard(Server* server_, wlr_keyboard* wlr_keyboard_ptr_, bool is_virtual_)
    : server(server_), wlr_keyboard_ptr(wlr_keyboard_ptr_), is_virtual(is_virtual_) {
  if (is_virtual) {
    xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_keymap* keymap = xkb_keymap_new_from_names(context, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(wlr_keyboard_ptr, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(wlr_keyboard_ptr, 25, 600);
  }
  server->register_keyboard(this);
  server->apply_keyboard_config(this);  // keymap from keyboard.toml, repeat rate, current layout

  modifiers.notify = keyboard_modifiers;
  wl_signal_add(&wlr_keyboard_ptr->events.modifiers, &modifiers);

  key.notify = keyboard_key;
  wl_signal_add(&wlr_keyboard_ptr->events.key, &key);

  destroy.notify = keyboard_destroy;
  wl_signal_add(&wlr_keyboard_ptr->base.events.destroy, &destroy);

  server->notify_keyboard_added();
}

Keyboard::~Keyboard() {
  server->unregister_keyboard(this);
  wl_list_remove(&modifiers.link);
  wl_list_remove(&key.link);
  wl_list_remove(&destroy.link);
  server->notify_keyboard_removed();
}

bool Keyboard::handle_keybind(xkb_keysym_t sym) {
  bool shift_held = (wlr_keyboard_get_modifiers(wlr_keyboard_ptr) & WLR_MODIFIER_SHIFT) != 0;
  const Server::ResolvedKeybinds& binds = server->keybinds();

  // Desktop layout: only the terminal shortcut stays bound for now.
  // Desktop layout: every Alt (tiling) shortcut is off, Alt+Enter included. The
  // Desktop combos (terminal, apps, shortcuts list) are handled in keyboard_key.
  if (server->desktop_layout()) {
    return false;
  }

  if (sym == binds.terminal) {
    if (shift_held && !server->desktop_layout()) {
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
    spawn_terminal(server->default_apps_config().terminal_command.c_str());
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
  // The vim-style keys, and the arrow keys as well.
  const bool arrow = sym == XKB_KEY_Left || sym == XKB_KEY_Right || sym == XKB_KEY_Up || sym == XKB_KEY_Down;
  if (arrow || sym == binds.focus_left || sym == binds.focus_right || sym == binds.focus_up ||
      sym == binds.focus_down) {
    Direction dir = (sym == binds.focus_left || sym == XKB_KEY_Left)     ? Direction::Left
                     : (sym == binds.focus_right || sym == XKB_KEY_Right) ? Direction::Right
                     : (sym == binds.focus_up || sym == XKB_KEY_Up)       ? Direction::Up
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
