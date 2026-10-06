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
  // `is_virtual`: a keyboard made by a client (wtype, remote desktop); it brings its own keymap, so
  // the layout settings never touch it.
  Keyboard(Server* server, wlr_keyboard* wlr_keyboard_ptr, bool is_virtual = false);
  ~Keyboard();

  Server* server;
  wlr_keyboard* wlr_keyboard_ptr;
  bool is_virtual = false;

  wl_listener modifiers{};
  wl_listener key{};
  wl_listener destroy{};
  // A Super (Windows/Meta) key press with nothing pressed since: releasing it opens
  // the start menu in the Desktop layout.
  bool super_tap = false;
  // Alt and Shift went down together with no other key since: letting go switches the layout.
  bool layout_chord = false;
  bool alt_down = false, shift_down = false;  // tracked from key events for the chord

  // Returns true if the key event was consumed as a compositor keybind
  // (and should not be forwarded to the focused client).
  bool handle_keybind(xkb_keysym_t sym);
};

}  // namespace fleetwm
