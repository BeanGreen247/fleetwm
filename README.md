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

- Every window has a titlebar with pin, minimize, maximize and close buttons, drawn as one joined,
  glossy strip in the Windows 7 style (a wider red close button, a blue glow behind the button under the
  pointer and an orange one behind close), the same with glass effects on or off. You can change their
  size, which side they are on, and where the title sits.
- Drag a titlebar to move a window. Drag an edge or corner to resize. Double-click a titlebar to
  maximize.
- Drag a window to a screen edge to snap it: left or right edge for a half, a corner for a quarter,
  the top edge to maximize.
- The bar turns into a taskbar with a start button, one button per window, and the clock and system
  info on the right. It can sit on the bottom, top, left or right.
- Tap the Super (Windows) key, or click the start button, to open the start menu.
  It is laid out like Windows 7's: your default apps and Settings on the left ("All Programs" lists everything,
  and typing searches), your folders, Settings, a lock button and Shut down on the right.
- Alt+Tab shows a panel with a thumbnail of every window and frames the one you are switching to. Settings,
  Shortcuts and the language checklist are ordinary windows here; in the Tiling layout they stay on top.
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
repository). It applies to every terminal you start in the session, from the shortcut, the launcher or
the start menu. The shell setup keeps bash behaving normally and only adds a bigger history,
case-insensitive completion, history search on Up/Down and fzf keys when fzf is installed. If you
create your own `~/.config/foot/foot.ini`, foot uses that instead, and nothing of Fleetwm's is mixed in.
If a dotfile tool (Ansible, stow, chezmoi) puts a `foot.ini` there, that is what you get. To keep Fleetwm's
defaults and only change a few things, start your file with `include=/usr/local/etc/fleetwm/foot.ini`
(foot 1.12 or newer) and put your changes below it.

## Keep awake padlock

A padlock sits in the bar's tray (`fleetwm-lockapplet`, started with the session). Click it to
keep the screen on and stop the computer from sleeping; the padlock is crossed out while that is
on. Click again to go back to the timers from Settings -> Power. Hover it to see which timers are
active or paused and which programs are keeping the computer awake (a video player, a browser
playing video, or anything else that asks the compositor, systemd or `org.freedesktop.ScreenSaver`
to stay awake). The setting is not saved: it is off after every login, and it switches itself off
if the applet quits.

## Glass effects

Settings -> Theme -> Glass effects switches between a flat, matte look and a see-through, frosted one. Glass
applies to the bar (capsules, island and strip), the taskbar and its window buttons, window titlebars, the start menu and the Alt+Tab panel. The frost is a small blurred copy of your wallpaper that is
made once when the wallpaper changes and kept in `~/.cache/fleetwm`; surfaces only show the part of it behind them.
Nothing is blurred while you work, so glass costs almost nothing. It does not blur windows that sit behind a
surface, only the wallpaper.

## Keyboard

Settings -> Keyboard lists the layouts you switch between, in order, with the one in use marked.
Click a layout to switch to it, reorder or remove them with the buttons, and press "Add or remove
languages and layouts" to open a checklist of every layout (with its variants) and every language
the system knows, with a search box. Tick what you want and press Done: the layouts take effect
at once, and the locales and the font cache are built in the background so the new languages work
and stay fast. The system part runs through polkit, which the installer allows for members of the
`sudo`, `wheel` or `adm` group; for anyone else, run `sudo fleetwm-locale-build` once.

Switch layouts while you work with Win+Space (Win+Shift+Space goes back) and/or Alt+Shift, as in
Windows; Settings -> Keyboard picks which. The bar shows the layout in use as a small pill (`US`,
`CZ`): click it for the next layout, right-click for the keyboard settings, hover for the list.
The same page sets the key held for the Tiling layout shortcuts (Alt or Super), the key repeat
speed and delay, and has a field to try the keyboard in. Settings are saved in
`~/.config/fleetwm/keyboard.toml`.

## Network

The bar shows a network icon: a Wi-Fi fan lit to the signal strength when you are on Wi-Fi, or an
Ethernet port when you are on a cable. Hover it for the name, address and gateway, click it to
open Settings -> Network.

Settings -> Network lists every network card in its own block. Ethernet cards show the link state,
speed and address. Wi-Fi cards also list the networks in range: click one to connect (a password
field appears for secured networks), disconnect, or forget a saved one.

It works with whatever already manages your network. With NetworkManager (what GNOME, KDE and XFCE
use) it can do everything, including switching the Wi-Fi radio. With plain wpa_supplicant (netplan,
systemd-networkd) it handles Wi-Fi; add your user to the `netdev` group, which the installer does.
With neither, it shows what the system reports and nothing more. The installer adds the Wi-Fi tools
and firmware for common chips, and installs NetworkManager only when there is Wi-Fi hardware and
nothing manages it yet.

## Power menu

The power menu (Start menu -> Shut down, or the power icon in the bar) has Lock, Log out, Sleep, Reboot and
Shut down. Sleep, Reboot and Shut down need no password: the installer adds a polkit rule
(`/etc/polkit-1/rules.d/50-fleetwm-power.rules`) that allows them for whoever is at the keyboard (an active
session on a local seat), also when another user is logged in. Remote logins and background sessions are
not covered, and neither are the "ignore inhibitors" variants, so a program that asked the system to stay
awake (the keep-awake padlock) is still respected. Delete the file to get password prompts back; Date &
Time and the language builder have their own rules (`50-fleetwm-time.rules`, `50-fleetwm-locale.rules`)
for administrators. All of it is covered by `scripts/test.sh power`.

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

## Performance

Fleetwm is built to stay light and quick on weak hardware; the testing bed is a Celeron N4020 laptop.
Every item below was measured, and the numbers and the things that were tried and rejected are in
[docs/OPTIMIZATIONS.md](docs/OPTIMIZATIONS.md).

- **Idle costs nothing.** Nothing redraws unless something visible changed; the compositor, bar,
  wallpaper and applets sit at 0.0-0.3% CPU on the laptop with a seconds clock showing.
- **Small clients.** The bar, wallpaper, launcher, settings, locker and menus are plain Wayland clients
  on a tiny toolkit (no GTK, GLib or Pango): 4-17 MB each, and the always-resident shell (bar and
  wallpaper) went from about 260 MB to about 17 MB.
- **Seconds clock repaints a strip.** The bar redraws and reports only the clock's rectangle when it
  ticks: 0.72% to 0.28% CPU with glass on.
- **Glass (Windows Aero) titlebars are cached.** The glass background and the caption buttons are drawn once per
  state and copied, so moving the pointer across the buttons costs about a seventh of what it did and glass costs
  the same as the flat look.
- **Glass is cached.** Each glass rectangle is painted once and reused; the blurred wallpaper is decoded
  once per process. Glass on costs the bar about the same as glass off.
- **Cheap performance overlay.** One small picture repainted four times a second inside the frame being
  committed, instead of hundreds of scene rectangles that kept the compositor rendering by themselves:
  58% to 0.27% CPU idle on the laptop, 37.8% to 6.5% under a busy terminal.
- **Start menu is only as big as its card.** About a quarter of the compositor memory it used before,
  and the launcher's own memory is down about 15%.
- **GPU buffers can go straight to the screen.** The compositor tells GPU clients which formats the
  display can scan out, and offers viewporter and single-pixel buffers, so a fullscreen video or game
  does not have to be composited.
- **Build.** Release builds use LTO, `-march=native`, profile-guided optimization with automatic
  training, unity builds and speed-first flags; the compositor runs at raised priority.
- **Memory.** jemalloc with a background purge thread for the compositor, tuned glibc thresholds for
  everything else, the login screen on the software renderer, and the pixman renderer on machines
  without a GPU.
- **Hot paths.** Window border colours are parsed once per theme load, the cursor name is cached, the
  custom FPS cap never spins while idle, and the clock and the CPU/GPU stats run on separate timers.

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
