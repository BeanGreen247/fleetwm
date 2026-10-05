#pragma once

#include <string>
#include <vector>

#include "keybinds_config.hpp"
#include "theme.hpp"

// The data behind the "Keyboard Shortcuts" window (fleetwm-shortcuts): every
// binding with its current key (so remaps in keybinds.toml show up), grouped
// by section, and which of them are live in the active window layout.

namespace fleetwm {

struct ShortcutEntry {
  std::string section;
  std::string keys;         // e.g. "Alt+Shift+Q" or "Drag a titlebar"
  std::string description;
  bool active = true;       // false: exists but is off in the current window layout
};

// Where the documentation lives: the installed copy if there is one, else the
// project page.
extern const char* const kDocsUrl;
extern const char* const kShortcutsDocUrl;

// "Alt+<key>" for a keybinds.toml key name, with Shift added for an uppercase
// single letter ("Q" -> "Alt+Shift+Q") and for names that need Shift to type
// ("question" -> "Alt+Shift+/"). Common names are shown the way keycaps are
// labelled: Return -> Enter, Escape -> Esc, single letters in upper case.
std::string format_alt_combo(const std::string& keysym_name);

// "Super+<key>": the Windows/Meta-key binds (app shortcuts and the shortcuts list).
std::string format_super_combo(const std::string& keysym_name);

// `modifier` is the keybinds.toml modifier setting ("super", "ctrl+alt", ...);
// app shortcuts pass force_shift so they read e.g. "Super+Shift+B".
std::string format_mod_combo(const std::string& modifier, const std::string& keysym_name,
                             bool force_shift = false);

// Same, but always with Shift (the "promote to master" variant of the terminal key).
std::string format_alt_shift_combo(const std::string& keysym_name);

// All shortcuts and mouse gestures for `layout`. In the Desktop layout only
// the terminal and this window's own shortcut are active; the rest are listed
// as inactive so people can see what the Tiling layout offers.
std::vector<ShortcutEntry> build_shortcut_list(const KeybindsConfig& binds, WindowLayout layout);

}  // namespace fleetwm
