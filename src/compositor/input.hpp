#pragma once

#include <wayland-server-core.h>

extern "C" {
#include <wlr/types/wlr_keyboard.h>
}

namespace fleetwm {

class Server;

// One keyboard input device. wlroots' libinput backend hands us one of
// these per physical (or virtual, e.g. Xwayland-in-Wayland nested) keyboard;
// Phase 0 handles them uniformly rather than modeling multi-keyboard
// layouts/groups, which is not a stated v1 requirement.
class Keyboard {
 public:
  Keyboard(Server* server, wlr_keyboard* wlr_keyboard_ptr);
  ~Keyboard();

  Server* server;
  wlr_keyboard* wlr_keyboard_ptr;

  wl_listener modifiers{};
  wl_listener key{};
  wl_listener destroy{};

  // Returns true if the key event was consumed as a compositor keybind
  // (and should not be forwarded to the focused client).
  bool handle_keybind(xkb_keysym_t sym);
};

}  // namespace fleetwm
