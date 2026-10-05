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

The script installs what it needs with `apt`, builds the project and installs it to
`/usr/local`. Log out and back in afterwards (the install adds you to the `input`, `video`,
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
| Super+/ | Show all shortcuts |

Workspaces are switched by clicking the numbers in the bar. Gaps between windows, at the screen
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
- Switching from Tiling to Desktop keeps your windows where they were.

Shortcuts in this layout (none of the Alt shortcuts above work here):

| Keys | What it does |
| --- | --- |
| Ctrl+Alt+T | Open a terminal |
| Super+Shift+B | Web browser |
| Super+Shift+E | File manager |
| Super+Shift+T | Text editor |
| Super (tap) | Start menu |
| Super+/ | Show all shortcuts |

The browser, file manager and editor are the ones you pick in Settings under Default Apps.

## Settings

Open Settings from the launcher or the start menu. Changes apply right away, and they are saved
for your user in `~/.config/fleetwm/`. Settings that belong to the other layout are greyed out.

All shortcuts can be changed in `~/.config/fleetwm/keybinds.toml`. The full list and the file
format are in [docs/SHORTCUTS.md](docs/SHORTCUTS.md). The shortcuts are also listed in the app
that opens with Super+/.

## Login screen

Fleetwm has an optional login screen, `fleetwm-greet`. It is installed but off by default. See
[docs/GREETER.md](docs/GREETER.md) to turn it on. You can also just use your normal display
manager.

## More documentation

- [Shortcuts](docs/SHORTCUTS.md)
- [Building, testing and how it is put together](docs/DEVELOPMENT.md)
- [The original feature list](docs/FEATURES.md)
- [Hardware it has been tried on](docs/HARDWARE.md)
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
