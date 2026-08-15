#include "input.hpp"

#include <unistd.h>
#include <wayland-server-core.h>
#include <xkbcommon/xkbcommon.h>

extern "C" {
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
}

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "server.hpp"

namespace fleetwm {

namespace {

// Alt+Return rather than Super+Return: Phase 0 is routinely run nested
// inside an existing host compositor/WM for dev iteration (per the Phase 0
// roadmap entry), and Super is frequently already claimed by the host
// desktop's own shortcuts. Alt+Return has no such conflict in the common
// nested-Wayland or nested-X11 dev setups this phase targets.
constexpr xkb_keysym_t kSpawnTerminalKey = XKB_KEY_Return;

constexpr const char* kTerminalCommand = "foot";  // lightweight Wayland-native terminal

void spawn_terminal() {
  pid_t pid = fork();
  if (pid < 0) {
    std::fprintf(stderr, "fleetwm: fork for terminal spawn failed: %s\n", std::strerror(errno));
    return;
  }
  if (pid == 0) {
    execlp(kTerminalCommand, kTerminalCommand, nullptr);
    // execlp only returns on failure -- log why before the child dies, since
    // this failure would otherwise be completely silent.
    std::fprintf(stderr, "fleetwm: failed to exec '%s': %s\n", kTerminalCommand,
                 std::strerror(errno));
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

  if (alt_held && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
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
  if (sym == kSpawnTerminalKey) {
    spawn_terminal();
    return true;
  }
  if (sym == XKB_KEY_Escape) {
    wl_display_terminate(server->display());
    return true;
  }
  return false;
}

}  // namespace fleetwm
