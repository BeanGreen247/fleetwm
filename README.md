# Fleetwm

Fleetwm is a small, fast Wayland desktop for Debian and Ubuntu, built on
[wlroots](https://gitlab.freedesktop.org/wlroots/wlroots). It comes with its own bar, settings
app, app launcher, wallpaper, power menu, lock screen, audio mixer and login screen. GTK, Qt and
X11 apps all run on it. It is early software, so expect rough edges.

You can use it in two ways and switch at any time in Settings:

- **Tiling** (the default): windows tile for you, and you drive everything from the keyboard.
- **Desktop**: ordinary floating windows with titlebars, a taskbar and a start menu, like a
  traditional desktop.

## Install

You need Debian 13 (Trixie) or Ubuntu/Kubuntu 26.04.

```sh
git clone https://github.com/BeanGreen247/fleetwm.git
cd fleetwm
./install.sh
```

Run it as your normal user, not with `sudo`; it asks for your password when it needs it. It
installs what it needs with `apt`, builds the project and installs it to `/usr/local`. The build
runs the tests first and takes a while on a small machine (about 25 minutes on a 2-core Celeron). Log out and back in afterwards (the install adds you to the `input`, `video`,
`render` and `audio` groups, which only takes effect on a new login), then pick **Fleetwm**
from your login screen's session list.

To update later, run `fleetwm-update`.

## Tiling layout

The first window fills the screen. More windows share the right half. The main keys:

| Keys | What it does |
| --- | --- |
| Alt+Enter | Open a terminal |
| Alt+D | App launcher |
| Alt+Shift+Q | Close the window |
| Alt+H / J / K / L | Move focus left / down / up / right |
| Alt+Shift+Enter | Make this window the main one |
| Alt+Shift+F | Float or tile this window |
| Alt+Shift+P | Pin this window on top |
| Alt+Shift+L | Lock the screen |
| Alt+Shift+S | Screenshot a region |
| Alt+Escape | Quit |
| Alt+Tab | Switch between windows on this workspace |
| Super+1 ... 0 | Go to workspace 1 ... 10 |
| Super+Shift+1 ... 0 | Send the window to that workspace |
| Super+Shift+Left / Right | Send the window to another screen |
| Super+/ | Show all shortcuts |

You can also click the workspace numbers in the bar. Gaps between windows, at the screen
edges and next to the bar can be changed in Settings.

## Desktop layout

- Every window has a titlebar with pin, minimize, maximize and close buttons. You can change their
  size, which side they are on, and where the title sits.
- Drag a titlebar to move a window. Drag an edge or corner to resize. Double-click a titlebar to
  maximize.
- Drag a window to a screen edge to snap it: left or right edge for a half, a corner for a quarter,
  the top edge to maximize.
- The bar turns into a taskbar with a start button, one button per window, and the clock and system
  info on the right. It can sit on the bottom, top, left or right.
- Tap the Super (Windows) key, or click the start button, to open the start menu.
- There are several workspaces here too: numbered buttons on the taskbar (Settings sets how many are shown), plus the keys below. The taskbar lists the windows of the current workspace.
- Switching from Tiling to Desktop keeps your windows where they were.
- X11 programs (through XWayland) are normal windows too: they tile, float, snap, get titlebars in this layout and show up on the taskbar.
- The battery shows its percentage in the bar. Hover it to see how much time is left; click it (or the plug icon on a desktop) to open Settings -> Power, where you set when the display turns off and when the computer sleeps, separately for mains power and battery.

Shortcuts in this layout (none of the Alt shortcuts above work here):

| Keys | What it does |
| --- | --- |
| Ctrl+Alt+T | Open a terminal |
| Super+Shift+B | Web browser |
| Super+Shift+E | File manager |
| Super+Shift+T | Text editor |
| Super (tap) | Start menu |
| Super+Left / Right / Up / Down | Snap, maximize, restore (like Windows) |
| Alt+Tab | Switch between windows on this workspace |
| Alt+F4 | Close the window |
| Super+D | Show the desktop |
| Super+1 ... 0, Ctrl+Alt+Left / Right | Switch workspace |
| Super+Shift+1 ... 0 | Send the window to a workspace |
| Super+Shift+Left / Right | Send the window to another screen |
| Super+/ | Show all shortcuts |

The browser, file manager and editor are the ones you pick in Settings under Default Apps.

## Workspaces

The bar starts with workspaces 1 to 4. Go to a workspace that is not shown (for example
Super+6) and its button appears; put a window on it and it stays. Leave a workspace empty and
its button goes away again. The buttons are always in numerical order, so 1 2 3 4 6 becomes
1 2 3 4 5 6 when you use 5. This works the same in both layouts.

## Terminal

Plain `foot` opens with a larger font and a short prompt (`~>`, plus the git branch inside a
repository). The shell setup keeps bash behaving normally and only adds a bigger history,
case-insensitive completion, history search on Up/Down and fzf keys when fzf is installed. If you
create your own `~/.config/foot/foot.ini`, foot uses that instead.

## Settings

Open Settings from the launcher or the start menu. Changes apply right away, and they are saved
for your user in `~/.config/fleetwm/`. Settings that belong to the other layout are greyed out.

All shortcuts can be changed in `~/.config/fleetwm/keybinds.toml`. The full list and the file
format are in [docs/SHORTCUTS.md](docs/SHORTCUTS.md). The shortcuts are also listed in the app
that opens with Super+/.

## Dark and light mode for other apps

Picking the Light theme in Settings tells GTK apps, Chromium and Qt apps to use their light
look, and every other theme uses the dark look. This works through GTK's settings, GSettings
and `xdg-desktop-portal-gtk` (the installer adds them). Apps read the setting when they start,
so reopen an already running app after changing the theme. Plain X11 programs have no theme
setting of their own.

## Login screen

The installer turns on the Fleetwm login screen (`fleetwm-greet`) for the first text console
(tty1). Reboot to see it, or start it right away with
`sudo systemctl start fleetwm-greeter@tty1.service`. Ctrl+Alt+F2 and up still give you a normal
text login if you ever need one.

It is skipped if a display manager (GDM, SDDM, LightDM) is already enabled, in which case pick
Fleetwm from that login screen's session list. Set `FLEETWM_NO_GREETER=1` when running the
installer to skip it. To switch it off later, see [docs/GREETER.md](docs/GREETER.md).

## More documentation

- [Shortcuts](docs/SHORTCUTS.md)
- [Building, testing and how it is put together](docs/DEVELOPMENT.md)
- [The original feature list](docs/FEATURES.md)
- [Hardware it has been tried on](docs/HARDWARE.md) (including a Celeron laptop used as the real-hardware testing bed)
- [Memory and speed notes](docs/OPTIMIZATIONS.md)
- [Design decisions](docs/adr)

To run the unit tests: `scripts/test.sh`. Add an area name to run part of them, for example
`scripts/test.sh geometry`.

## Credits

Single-board-computer testing runs on
[eqvaldi/releases V4-LTS-3](https://github.com/eqvaldi/releases/releases/tag/V4-LTS-3). Thanks to
[eqvaldi](https://github.com/eqvaldi) for it.

## License

MIT

## Support

If you find this useful, you can support it on PayPal:

[![Donate with PayPal](.github/paypal-qr.png)](https://paypal.me/beangreen2471)

https://paypal.me/beangreen2471
