#include "shortcut_list.hpp"

#include <cctype>

namespace fleetwm {

const char* const kDocsUrl = "https://github.com/BeanGreen247/fleetwm#readme";
const char* const kShortcutsDocUrl =
    "https://github.com/BeanGreen247/fleetwm/blob/master/docs/SHORTCUTS.md";

static std::string format_combo(const std::string& name, bool force_shift, const char* mod = "Alt") {
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
  return std::string(mod) + "+" + (shift ? "Shift+" : "") + key;
}

std::string format_alt_combo(const std::string& name) { return format_combo(name, false); }

std::string format_alt_shift_combo(const std::string& name) { return format_combo(name, true); }

std::string format_key_combo(const std::string& combo) {
  const KeyCombo parsed = parse_key_combo(combo);
  if (!parsed.valid) return "(unbound)";
  // Conventional order: Super, Ctrl, Alt, Shift, then the key.
  std::string label;
  if (parsed.mods & kModLogo) label += "Super+";
  if (parsed.mods & kModCtrl) label += "Ctrl+";
  if (parsed.mods & kModAlt) label += "Alt+";
  bool shift = parsed.mods & kModShift;
  // key_label() adds Shift itself for shifted names ("Q"), so reuse format_combo's
  // key naming and strip its "Alt+" prefix.
  std::string key = format_combo(parsed.key, shift);
  key = key.substr(4);  // drop "Alt+"
  return label + key;
}

std::vector<ShortcutEntry> build_shortcut_list(const KeybindsConfig& b, WindowLayout layout) {
  const bool desktop = layout == WindowLayout::Desktop;
  std::vector<ShortcutEntry> out;
  auto add = [&](const char* section, std::string keys, const char* what, bool desktop_ok = false) {
    out.push_back({section, std::move(keys), what, !desktop || desktop_ok});
  };

  // The terminal key differs per layout: Alt+Enter in Tiling, its own combo in Desktop.
  add("Applications", desktop ? format_key_combo(b.desktop_terminal) : format_alt_combo(b.terminal),
      "Open a terminal", true);
  if (desktop) {  // Desktop-layout app shortcuts (not bound in Tiling)
    add("Applications", format_key_combo(b.desktop_browser), "Open the web browser", true);
    add("Applications", format_key_combo(b.desktop_file_manager), "Open the file manager", true);
    add("Applications", format_key_combo(b.desktop_text_editor), "Open the text editor", true);
    {
      const std::vector<std::string> keys = split_key_names(b.start_menu_key);
      std::string label = keys.empty() ? "Super" : keys.front();
      for (const char* suffix : {"_L", "_R"})
        if (label.size() > 2 && label.compare(label.size() - 2, 2, suffix) == 0) label.resize(label.size() - 2);
      add("Applications", label + " (tap)", "Open or close the start menu", true);
    }
  }
  add("Applications", format_alt_combo(b.launcher), "Application launcher");
  add("Applications", format_alt_combo(b.screenshot), "Screenshot a region to the clipboard");
  add("Applications", format_alt_combo(b.lock), "Lock the screen");
  add("Help", format_key_combo(b.shortcuts_help), "Show this list of shortcuts", true);

  add("Windows", format_alt_combo(b.close_window), "Close the focused window");
  add("Windows", format_alt_combo(b.toggle_pin), "Pin the focused window (always on top, on every workspace)");
  add("Windows", format_alt_combo(b.toggle_float), "Float or tile the focused window");
  add("Windows", format_alt_shift_combo(b.terminal), "Make the focused window the master (tiling)");

  // Window and workspace management that works in both layouts.
  add("Switching", format_key_combo(b.cycle_windows),
      "Switch between the windows on this workspace (hold the modifier, tap Tab again to go further)", true);
  add("Switching", format_key_combo(b.cycle_windows_reverse), "Switch between the windows on this workspace, the other way", true);
  add("Switching", format_key_combo(b.workspace_switch + "+1") + " to " + format_key_combo(b.workspace_switch + "+0").substr(format_key_combo(b.workspace_switch + "+0").rfind('+') + 1),
      "Go to workspace 1 to 10", true);
  add("Switching", format_key_combo(b.workspace_send + "+1") + " to " + format_key_combo(b.workspace_send + "+0").substr(format_key_combo(b.workspace_send + "+0").rfind('+') + 1),
      "Send the window to workspace 1 to 10", true);
  add("Switching", format_key_combo(b.workspace_prev), "Previous workspace", true);
  add("Switching", format_key_combo(b.workspace_next), "Next workspace", true);
  add("Switching", format_key_combo(b.send_to_prev_screen), "Send the window to the previous screen", true);
  add("Switching", format_key_combo(b.send_to_next_screen), "Send the window to the next screen", true);
  if (desktop) {
    add("Windows", format_key_combo(b.desktop_close_window), "Close the focused window", true);
    add("Windows", format_key_combo(b.desktop_toggle_maximize), "Maximize or restore the focused window", true);
    add("Windows", format_key_combo(b.desktop_show_desktop), "Show the desktop (minimize all); again brings them back", true);
    add("Windows", format_key_combo(b.desktop_minimize_all), "Minimize all windows", true);
    add("Windows", format_key_combo(b.desktop_restore_all), "Restore all windows", true);
    add("Snapping", format_key_combo(b.desktop_snap_left), "Snap left; again moves to the screen on the left", true);
    add("Snapping", format_key_combo(b.desktop_snap_right), "Snap right; again moves to the screen on the right", true);
    add("Snapping", format_key_combo(b.desktop_snap_up), "Top half, then maximize (from a left/right half: its top quarter)", true);
    add("Snapping", format_key_combo(b.desktop_snap_down), "Restore from maximized, then bottom half (from a left/right half: its bottom quarter). Never minimizes", true);
  }

  add("Focus", format_alt_combo(b.focus_left), "Focus the window to the left (Alt+Arrow keys work too)");
  add("Focus", format_alt_combo(b.focus_down), "Focus the window below");
  add("Focus", format_alt_combo(b.focus_up), "Focus the window above");
  add("Focus", format_alt_combo(b.focus_right), "Focus the window to the right");

  add("Session", format_alt_combo(b.quit), "Quit fleetwm (log out)");
  add("Session", desktop ? format_key_combo(b.desktop_debug_overlay) : format_alt_combo(b.debug_overlay),
      "Toggle the frame-time / FPS / RAM overlay", true);

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
