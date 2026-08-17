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

// Alt+Return rather than Super+Return: Phase 0 is routinely run nested
// inside an existing host compositor/WM for dev iteration (per the Phase 0
// roadmap entry), and Super is frequently already claimed by the host
// desktop's own shortcuts. Alt+Return has no such conflict in the common
// nested-Wayland or nested-X11 dev setups this phase targets.
constexpr xkb_keysym_t kSpawnTerminalKey = XKB_KEY_Return;

// Alt+D, dmenu-style mnemonic -- same Super-conflict rationale as
// kSpawnTerminalKey above applies here too.
constexpr xkb_keysym_t kSpawnLauncherKey = XKB_KEY_d;

constexpr const char* kLauncherCommand = "fleetwm-launcher";

// Alt+Shift+Q: closest available match to i3's default Mod+Shift+kill
// bind, since Phase 0 has no Super/Mod4 support yet (same Alt-only
// convention as the spawn binds above). xkb_state_key_get_syms already
// resolves Shift into the keysym itself (Shift+q -> XKB_KEY_Q), so no
// separate shift_held check is needed here.
constexpr xkb_keysym_t kCloseWindowKey = XKB_KEY_Q;

// Alt+Shift+P: toggles PowerToys-style always-on-top pinning for the
// focused window. Same Shift-resolved-into-keysym note as
// kCloseWindowKey above applies.
constexpr xkb_keysym_t kTogglePinKey = XKB_KEY_P;

// dwm-style master-stack tiling binds (Output::relayout()). Alt+J/K cycle
// focus through server->views in stacking order; unlike
// kCloseWindowKey/kTogglePinKey these are NOT Shift-combined, so lowercase
// j/k (not J/K) is what xkb_state_key_get_syms resolves to.
constexpr xkb_keysym_t kFocusNextKey = XKB_KEY_j;
constexpr xkb_keysym_t kFocusPrevKey = XKB_KEY_k;

// Alt+Shift+F: opts the focused window out of tiling (or back in),
// independent of pinned -- see View::floating.
constexpr xkb_keysym_t kToggleFloatKey = XKB_KEY_F;

// Alt+Shift+L: locks the session (same action as the bar's power-menu
// "Lock" entry, bar_window.cpp's build_power_menu()). Same
// Shift-resolved-into-keysym convention as kTogglePinKey/kToggleFloatKey
// above. Deliberately still dispatched through the normal
// !is_locked()-gated branch in keyboard_key() like every other Alt+<key>
// bind -- if the session is already locked, Alt+Shift+L (like every
// other global keybind) is a no-op rather than reaching this handler at
// all, so there's no separate "already locked" check needed here.
constexpr xkb_keysym_t kLockKey = XKB_KEY_L;

// Alt+Shift+S: region-select screenshot, copied to the clipboard with a
// desktop notification -- same grim+slurp+wl-copy+notify-send combo (and
// $mod+Shift+s binding) the project's own sway config used
// (github.com/BeanGreen247/sway-setup-script), ported to this
// compositor's Alt-based convention. Runs via spawn_shell() (not
// spawn()) since it needs a pipe, not a bare argv-less binary.
constexpr xkb_keysym_t kScreenshotKey = XKB_KEY_S;
constexpr const char* kScreenshotCommand =
    "grim -g \"$(slurp)\" - | wl-copy && notify-send 'Screenshot' 'Copied to clipboard'";

// Alt+Shift+Return: promotes the focused window to master. Deliberately
// checked via an explicit shift_held flag (unlike kCloseWindowKey/
// kTogglePinKey's "Shift resolves into the keysym" shortcut) because plain
// Alt+Return without Shift is already kSpawnTerminalKey and Return has no
// separate shifted keysym to distinguish them by.
constexpr xkb_keysym_t kPromoteKey = XKB_KEY_Return;

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

  if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    std::fprintf(stderr, "fleetwm: key press keycode=%u alt_held=%d nsyms=%d\n", keycode,
                 alt_held, nsyms);
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

  if (sym == kSpawnTerminalKey) {
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
  if (sym == kSpawnLauncherKey) {
    spawn(kLauncherCommand);
    return true;
  }
  if (sym == kCloseWindowKey) {
    if (View* view = focused_view(server)) {
      view->close();
    }
    return true;
  }
  if (sym == kTogglePinKey) {
    if (View* view = focused_view(server)) {
      view->set_pinned(!view->pinned);
    }
    return true;
  }
  if (sym == kLockKey) {
    server->request_lock();
    return true;
  }
  if (sym == kScreenshotKey) {
    spawn_shell(kScreenshotCommand);
    return true;
  }
  if (sym == kToggleFloatKey) {
    if (View* view = focused_view(server)) {
      view->set_floating(!view->floating);
      if (view->output) {
        view->output->relayout();
      }
    }
    return true;
  }
  if (sym == kFocusNextKey || sym == kFocusPrevKey) {
    if (server->views.empty()) {
      return true;
    }
    View* current = focused_view(server);
    // server->views is stacking-ordered (front = topmost/focused), which
    // for a freshly-tiled workspace also matches master-then-stack order
    // -- walking it directly gives "next/prev in the tiled set" without
    // needing a separate per-workspace ordering.
    auto it = server->views.begin();
    if (current) {
      it = std::find_if(server->views.begin(), server->views.end(),
                         [current](const std::unique_ptr<View>& v) { return v.get() == current; });
    }
    if (it == server->views.end()) {
      it = server->views.begin();
    }
    if (sym == kFocusNextKey) {
      ++it;
      if (it == server->views.end()) {
        it = server->views.begin();
      }
    } else {
      if (it == server->views.begin()) {
        it = std::prev(server->views.end());
      } else {
        --it;
      }
    }
    server->focus_view(it->get());
    return true;
  }
  if (sym == XKB_KEY_Escape) {
    wl_display_terminate(server->display());
    return true;
  }
  return false;
}

}  // namespace fleetwm
