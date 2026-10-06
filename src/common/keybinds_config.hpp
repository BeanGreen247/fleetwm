#pragma once

#include <string>
#include <vector>

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
  // Toggles the compositor's per-frame debug overlay (a bar graph of
  // recent frame times, see Output::update_debug_overlay() in
  // output.cpp) -- a developer/debugging tool, not a user-facing
  // feature, hence "I" for "info" rather than anything more prominent.
  std::string debug_overlay = "I";
  // ---- Desktop (floating) layout shortcuts ----
  // Each is a full combo written as '+'-joined modifier names followed by an xkb key
  // name, e.g. "ctrl+alt+t" or "super+shift+b". Modifiers: super (the Windows/Meta
  // key; also logo/win/meta), alt, ctrl (control), shift. They are independent of the
  // Alt-based Tiling shortcuts above, which are all switched off in the Desktop layout
  // (so Alt+Enter does not open a terminal there). The defaults stay clear of what runs
  // inside a terminal: tmux's Alt bindings (Alt+1..5, Alt+n/p/o, Alt+arrows), readline's
  // (Alt+b/f/d/t/u/l/c/?) and tmux's own prefix, Ctrl+b.
  std::string desktop_terminal = "ctrl+alt+t";
  std::string desktop_browser = "super+shift+b";       // default web browser (Settings -> Default Apps)
  std::string desktop_file_manager = "super+shift+e";  // default file manager
  std::string desktop_text_editor = "super+shift+t";   // default text editor
  // The frame-time / FPS / RAM overlay (Alt+Shift+I in the Tiling layout).
  std::string desktop_debug_overlay = "ctrl+alt+i";
  // Windows-style snapping with the arrow keys (Desktop layout): left/right half,
  // up maximizes, down restores or minimizes; at the screen edge the same key again
  // moves the window to the neighbouring screen.
  std::string desktop_snap_left = "super+Left";
  std::string desktop_snap_right = "super+Right";
  std::string desktop_snap_up = "super+Up";
  std::string desktop_snap_down = "super+Down";
  // More Windows-style window keys (Desktop layout).
  std::string desktop_close_window = "alt+F4";
  std::string desktop_toggle_maximize = "alt+F10";
  std::string desktop_show_desktop = "super+d";      // minimize everything, press again to bring it back
  std::string desktop_minimize_all = "super+m";
  std::string desktop_restore_all = "super+shift+m";

  // ---- Both layouts ----
  // Step through the open windows, most recently used first, like Alt+Tab: hold the
  // modifier and tap Tab again to go further; releasing it settles on that window.
  // Minimized windows are brought back, and a window on another workspace takes you there.
  std::string cycle_windows = "alt+Tab";
  std::string cycle_windows_reverse = "alt+shift+Tab";
  // Send the focused window to the previous / next screen (when there is more than one).
  std::string send_to_prev_screen = "super+shift+Left";
  std::string send_to_next_screen = "super+shift+Right";
  // Workspaces are the digit keys 1..9 and 0: <switch>+digit changes workspace,
  // <send>+digit moves the focused window there.
  std::string workspace_switch = "super";
  std::string workspace_send = "super+shift";
  // Previous / next workspace (Cinnamon's keys).
  std::string workspace_prev = "ctrl+alt+Left";
  std::string workspace_next = "ctrl+alt+Right";
  // Opens the keyboard-shortcuts window (fleetwm-shortcuts) in either layout.
  std::string shortcuts_help = "super+slash";
  // Switch to the next / previous keyboard layout (Windows' Win+Space). Which of these and Alt+Shift
  // are active is chosen in Settings -> Keyboard (keyboard.toml).
  std::string keyboard_next_layout = "super+space";
  std::string keyboard_prev_layout = "super+shift+space";
  // The key held for the Tiling layout's shortcuts (Alt+Return, Alt+D, ...): "alt" or "super".
  // Settings -> Keyboard changes it.
  std::string tiling_modifier = "alt";
  // Key(s) that open the start menu when tapped on their own (Desktop layout), as
  // xkb keysym names separated by commas. Remap it here if your keyboard has no
  // Super key, e.g. "Menu" or "F12".
  std::string start_menu_key = "Super_L,Super_R";
};

// Bit values of wlr_keyboard_modifiers (WLR_MODIFIER_*), so the result can be
// compared directly with the compositor's modifier state.
enum ModifierBit : unsigned { kModShift = 1, kModCtrl = 4, kModAlt = 8, kModLogo = 64 };

// "super+shift" -> kModLogo | kModShift. Names are case-insensitive; "super",
// "logo", "win" and "meta" all mean the Windows key, "control" is ctrl. Returns 0
// when the string is empty or contains an unknown name (callers then fall back to
// Super).
unsigned modifier_mask(const std::string& names);

// A parsed combo string such as "ctrl+alt+t": the modifier bits and the key name.
struct KeyCombo {
  unsigned mods = 0;
  std::string key;    // xkb keysym name, e.g. "t", "slash", "F12"
  bool valid = false;
};

// Everything before the last '+' is a modifier name (see modifier_mask), the last
// part is the key. Invalid (valid == false) when empty, when a modifier is unknown,
// or when there is no key ("ctrl+alt+").
KeyCombo parse_key_combo(const std::string& combo);

// Whether the modifiers held (WLR_MODIFIER_* bits, which may include Caps/Num lock)
// are exactly the combo's: only Shift, Ctrl, Alt and Super are compared, so Num
// Lock does not matter but an extra Shift does.
bool combo_mods_match(unsigned held, unsigned wanted);

// Workspace index (0..9) for a digit key name or character: "1".."9" -> 0..8,
// "0" -> 9, anything else -> -1. Matches the 1..9, 0 key order on the keyboard.
int workspace_index_for_key(const std::string& key);

// Splits "Super_L,Super_R" into trimmed, non-empty names.
std::vector<std::string> split_key_names(const std::string& list);

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
