#include "shortcut_list.hpp"

#include <cctype>

namespace fleetwm {

const char* const kDocsUrl = "https://github.com/BeanGreen247/fleetwm#readme";
const char* const kShortcutsDocUrl =
    "https://github.com/BeanGreen247/fleetwm/blob/master/docs/SHORTCUTS.md";

static std::string format_combo(const std::string& name, bool force_shift) {
  if (name.empty()) return "(unbound)";
  bool shift = force_shift;
  std::string key = name;
  if (name.size() == 1 && std::isupper(static_cast<unsigned char>(name[0]))) {
    shift = true;  // "Q" means Shift+q
  } else if (name.size() == 1) {
    key = std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(name[0]))));
  } else if (name == "question") {
    shift = true;
    key = "/";
  } else if (name == "slash") {
    key = "/";
  } else if (name == "Return") {
    key = "Enter";
  } else if (name == "Escape") {
    key = "Esc";
  } else if (name == "space") {
    key = "Space";
  } else if (name == "comma") {
    key = ",";
  } else if (name == "period") {
    key = ".";
  } else if (name == "minus") {
    key = "-";
  } else if (name == "equal") {
    key = "=";
  }
  return std::string("Alt+") + (shift ? "Shift+" : "") + key;
}

std::string format_alt_combo(const std::string& name) { return format_combo(name, false); }

std::string format_alt_shift_combo(const std::string& name) { return format_combo(name, true); }

std::vector<ShortcutEntry> build_shortcut_list(const KeybindsConfig& b, WindowLayout layout) {
  const bool desktop = layout == WindowLayout::Desktop;
  std::vector<ShortcutEntry> out;
  auto add = [&](const char* section, std::string keys, const char* what, bool desktop_ok = false) {
    out.push_back({section, std::move(keys), what, !desktop || desktop_ok});
  };

  add("Applications", format_alt_combo(b.terminal), "Open a terminal", true);
  add("Applications", format_alt_combo(b.launcher), "Application launcher");
  add("Applications", format_alt_combo(b.screenshot), "Screenshot a region to the clipboard");
  add("Applications", format_alt_combo(b.lock), "Lock the screen");
  add("Help", format_alt_combo(b.shortcuts), "Show this list of shortcuts", true);

  add("Windows", format_alt_combo(b.close_window), "Close the focused window");
  add("Windows", format_alt_combo(b.toggle_pin), "Pin the focused window (always on top, on every workspace)");
  add("Windows", format_alt_combo(b.toggle_float), "Float or tile the focused window");
  add("Windows", format_alt_shift_combo(b.terminal), "Make the focused window the master (tiling)");

  add("Focus", format_alt_combo(b.focus_left), "Focus the window to the left");
  add("Focus", format_alt_combo(b.focus_down), "Focus the window below");
  add("Focus", format_alt_combo(b.focus_up), "Focus the window above");
  add("Focus", format_alt_combo(b.focus_right), "Focus the window to the right");

  add("Session", format_alt_combo(b.quit), "Quit fleetwm (log out)");
  add("Session", format_alt_combo(b.debug_overlay), "Toggle the frame-time debug overlay");

  if (desktop) {
    out.push_back({"Mouse", "Drag a titlebar", "Move the window", true});
    out.push_back({"Mouse", "Drag an edge or corner", "Resize the window", true});
    out.push_back({"Mouse", "Double-click a titlebar", "Maximize or restore", true});
    out.push_back({"Mouse", "Drag to a screen edge", "Snap: left/right half, top maximizes, corners are quarters", true});
    out.push_back({"Taskbar", "Click a window button", "Focus it; click again to minimize; click a minimized one to restore", true});
    out.push_back({"Taskbar", "Middle-click a window button", "Close that window", true});
    out.push_back({"Taskbar", "Click the start button", "Open the start menu (Esc or a click elsewhere closes it)", true});
  } else {
    out.push_back({"Mouse", "Hover a window", "Focus follows the mouse", true});
    out.push_back({"Mouse", "Click a workspace number in the bar", "Switch workspace", true});
  }
  return out;
}

}  // namespace fleetwm
