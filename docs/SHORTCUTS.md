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

Of the Alt shortcuts only **Alt+Enter** (terminal) is bound, plus these (the Super key never
clashes with tmux or the shell's readline bindings, which use Alt+letters):

| Keys | Action |
| --- | --- |
| Tap Super (Windows key) | Open or close the start menu |
| Super+/ | This shortcuts list |
| Super+Shift+B | Default web browser |
| Super+Shift+E | Default file manager |
| Super+Shift+T | Default text editor |

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

`~/.config/fleetwm/keybinds.toml` (per user). `modifier = "super"` changes the modifier for the
Super+ binds (`ctrl+alt`, `alt`, ...); `start_menu_key = "Super_L,Super_R"` is the key whose tap opens
the start menu (use e.g. `Menu` or `F12` on a keyboard without a Super key); `browser`, `file_manager`
and `text_editor` pick the app keys. Key names are xkb keysym names (`Return`, `d`,
`Q`, `question`, `F5`). Changes apply right away.
