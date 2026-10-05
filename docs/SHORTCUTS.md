# Shortcuts

Open the in-app list with **Super+/** (press again, or Esc, to close it). It reads
`~/.config/fleetwm/keybinds.toml`, so remapped keys show up. Every bind is Alt+key; an
uppercase letter in `keybinds.toml` means Alt+Shift+that letter.

## Tiling layout (default)

| Keys | Action |
| --- | --- |
| Alt+Enter | Open a terminal |
| Alt+Shift+Enter | Make the focused window the master |
| Alt+D | Application launcher |
| Alt+Shift+S | Screenshot a region to the clipboard |
| Alt+Shift+L | Lock the screen |
| Alt+Shift+Q | Close the focused window |
| Alt+Shift+P | Pin the focused window (always on top, on every workspace) |
| Alt+Shift+F | Float or tile the focused window |
| Alt+H / J / K / L | Focus left / down / up / right |
| Alt+Esc | Quit fleetwm |
| Alt+Shift+I | Toggle the frame-time debug overlay |
| Super+/ | Show the shortcuts window |

The mouse: hovering a window focuses it; click a workspace number in the bar to switch.

## Desktop layout (floating windows)

None of the Alt (tiling) shortcuts work here, Alt+Enter included. These do, and every one is a
combo you can change in `keybinds.toml` (the defaults avoid tmux's Alt bindings and Ctrl+B prefix,
and the shell's readline Alt+letter bindings):

| Keys | Action | Setting |
| --- | --- | --- |
| Ctrl+Alt+T | Open a terminal | `desktop_terminal` |
| Super+Shift+B | Default web browser | `desktop_browser` |
| Super+Shift+E | Default file manager | `desktop_file_manager` |
| Super+Shift+T | Default text editor | `desktop_text_editor` |
| Ctrl+Alt+I | Frame-time / FPS / RAM overlay | `desktop_debug_overlay` |
| Super+/ | This shortcuts list (both layouts) | `shortcuts_help` |
| Tap Super (Windows key) | Open or close the start menu | `start_menu_key` |

The app shortcuts use the apps chosen in Settings -> Default Apps (or the first installed one).
Everything else is done with the mouse:

| Gesture | Action |
| --- | --- |
| Drag a titlebar | Move the window |
| Drag an edge or corner | Resize |
| Double-click a titlebar | Maximize or restore |
| Drag to a screen edge | Snap: left/right half, top maximizes, corners are quarters |
| Pin button in the titlebar | Keep the window on top (the pin is filled in while pinned) |
| Taskbar button | Focus; click the focused one to minimize; click a minimized one to restore |
| Middle-click a taskbar button | Close that window |
| Start button | Start menu: search, all applications, Settings / Shortcuts / Lock / Power |

## Changing keys

`~/.config/fleetwm/keybinds.toml` (per user). The Desktop shortcuts are full combos: modifier names
joined with `+` and then an xkb key name, for example `desktop_terminal = "ctrl+alt+t"` or
`desktop_browser = "super+shift+b"`. Modifiers are `super` (also `logo`, `win`, `meta`), `alt`, `ctrl`
and `shift`; the combo must match exactly, so Ctrl+Alt+Shift+T does not trigger Ctrl+Alt+T.
`start_menu_key = "Super_L,Super_R"` is the key whose tap opens the start menu (use e.g. `Menu` or
`F12` on a keyboard without a Super key). The Tiling shortcuts (`terminal`, `launcher`, ...) are the
Alt+key names above, e.g. `Q` for Alt+Shift+Q. Changes apply right away.
