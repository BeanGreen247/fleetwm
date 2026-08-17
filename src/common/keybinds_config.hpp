#pragma once

#include <string>

namespace fleetwm {

// Every fleetwm keybind is Alt+<key> (or Alt+Shift+<key> for the
// shift-combined ones) -- see input.cpp's own doc comment on why Alt,
// not Super, is the held modifier. This config only makes the <key>
// part remappable, not the modifier itself: a full arbitrary-modifier
// rebind system would be a much larger rearchitecture of
// Keyboard::handle_keybind's single alt_held gate (input.cpp), and
// wasn't asked for -- "remappable keybinds" here means "change which
// key does what", matching how i3/sway/dwm users actually customize
// binds day to day.
//
// Each field holds an xkb keysym *name* (resolved via
// xkb_keysym_from_name() -- see keybinds_from_config() below), not a
// raw keysym value, so the config file stays human-editable/readable.
// Uppercase letter names (e.g. "Q") mean "this key with Shift held",
// matching xkb's own naming and this codebase's existing
// Shift-resolves-into-the-keysym convention (see input.cpp's
// kCloseWindowKey/kTogglePinKey doc comments) -- lowercase names are
// the plain Alt-only binds.
struct KeybindsConfig {
  // Alt+<terminal>: spawn a terminal (Settings' Default Apps tab
  // decides which one, see default_apps.hpp). Alt+Shift+<terminal>:
  // promote the focused window to master -- intentionally the SAME key
  // as spawn-terminal, distinguished only by Shift, matching the
  // existing kSpawnTerminalKey behavior this replaces; not
  // independently remappable from it.
  std::string terminal = "Return";
  std::string launcher = "d";
  std::string close_window = "Q";
  std::string toggle_pin = "P";
  std::string toggle_float = "F";
  std::string lock = "L";
  std::string screenshot = "S";
  // Spatial directional focus (nearest tiled/visible window in that
  // screen direction from the currently focused one -- not a
  // stacking-order cycle), vim-style hjkl by default: h/l = left/right,
  // j/k = down/up.
  std::string focus_left = "h";
  std::string focus_down = "j";
  std::string focus_up = "k";
  std::string focus_right = "l";
  std::string quit = "Escape";
};

// Path helpers, mirroring default_apps.hpp's own pair but for
// keybinds.toml instead.
std::string keybinds_user_config_path();
std::string keybinds_system_default_config_path();

// Loads keybinds.toml. Returns KeybindsConfig{} (every field at its
// listed default above) if no config file exists anywhere yet, same
// fresh-install contract as load_default_apps_config().
KeybindsConfig load_keybinds_config();

// Writes `config` to the user config path, creating parent directories
// as needed. Throws std::runtime_error on I/O failure. No Settings UI
// writes this today -- provided for symmetry with the other config
// types and any future UI -- editing keybinds.toml directly is the
// expected path for now.
void save_keybinds_config(const KeybindsConfig& config);

}  // namespace fleetwm
