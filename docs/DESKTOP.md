# The desktop (`fleetwm-desktop`)

One small program per screen, started by the compositor with the bar and the wallpaper (`--output NAME`). What it shows depends on the window layout in Settings:

| | Desktop layout | Tiling layout |
|---|---|---|
| Icons | the contents of `~/Desktop` plus Computer, Home and Trash, in a grid from the top left | none |
| Right-click on the desktop | the Windows 7 menu | nothing |
| Shortcut card | none | a small card in the bottom right corner: `Shortcuts: [Super] [/]`, the keys of `shortcuts_help` in `keybinds.toml` |

Switching the layout takes effect at once (the program watches `theme.toml`): the icon surface is removed and the card appears, or the other way round.

## The menu (Desktop layout)
View > Large / Medium / Small icons, Auto arrange icons, Align icons to grid, **Show desktop icons** (hides every icon); Sort by > Name, Size, Item type, Date modified;
Refresh; Paste (files from the clipboard, copied into `~/Desktop`); New > Folder, Text Document (ready to rename); Open file manager; Display settings; Personalize.
On an icon: Open (bold), Open file location, Rename (F2), Delete (to the trash), Properties. Double-click opens (folders in `fleetwm-fm`, files with the default program),
rubber-band and Ctrl+A select, with Auto arrange off an icon can be dragged to another cell and stays there.

## Settings file
`~/.config/fleetwm/desktop.toml`, written by the menu: `show_icons`, `icon_size` (small, medium, large), `sort`, `ascending`, `auto_arrange`, `align_to_grid`, `show_computer`,
`show_home`, `show_trash`, `show_hint` (the shortcut card in the Tiling layout; edit the file to turn it off, there is no menu in that layout) and `[cells]` (icon -> column, row).

Code: `src/fleetfm/desktop.*` (listing, grid, sorting, hit tests, config; unit-tested), `apps/desktop/main.cpp` (drawing and input);
`fleetwm-desktop --screenshot out.png [--menu desktop|view|icon|none] [--hint]` draws it offline.
Not done: icons dragged between screens, desktop shortcuts keys other than F2, Delete, Enter, F5 and Ctrl+A, undo on the desktop, per-screen icon sets (every screen shows the same folder).
