#pragma once

// The pieces of the taskbar's right-click menu (fleetwm-ctxmenu) that are plain logic: the item strings the bar passes on the command
// line, what each kind does, where the menu goes next to the taskbar, and pinning an app in bar.toml.

#include <optional>
#include <string>
#include <vector>

#include "bar_config.hpp"

namespace fleetwm {

struct MenuItem {
  enum class Kind {
    Separator,
    Exec,   // arg: a program and its arguments separated by spaces
    Ipc,    // arg: a compositor command ("WINDOW_CLOSE 12")
    Pin,    // arg: a desktop entry id: add it to the taskbar's pinned apps
    Unpin,  // arg: a desktop entry id: remove it
  };
  Kind kind = Kind::Separator;
  std::string label, arg;
  bool operator==(const MenuItem&) const = default;
};

// "Label|exec|prog arg", "Label|ipc|COMMAND", "Label|pin|id.desktop", "Label|unpin|id.desktop" or "-" (separator). nullopt when malformed.
std::optional<MenuItem> parse_menu_item(const std::string& text);
std::string format_menu_item(const MenuItem& item);

// Splits an exec argument into program and arguments at spaces (no quoting: the bar only builds simple commands).
std::vector<std::string> split_exec_arg(const std::string& arg);

// Adds or removes `id` in config->pinned_apps. Returns true when the list changed.
bool apply_pin(BarConfig* config, const std::string& id, bool pin);

// Where the menu's surface goes. `edge` is where the taskbar is, `along` the coordinate of the click along that edge (x for a top or
// bottom taskbar, y for a left or right one); the menu opens on the desktop side of the taskbar, centred on the click and kept on the screen.
struct MenuSpot {
  unsigned anchor = 0;  // PopupAnchor bits (popup_spot.hpp)
  int top = 0, right = 0, bottom = 0, left = 0;
};
MenuSpot ctx_menu_spot(TaskbarPosition edge, int along, int menu_w, int menu_h, int screen_w, int screen_h, int gap = 6, int screen_edge = 8);

// Size of a menu with these items, from a row height and a text width the caller measured.
struct MenuSize {
  int w, h;
};
MenuSize ctx_menu_size(const std::vector<MenuItem>& items, int widest_label_px);

}  // namespace fleetwm
