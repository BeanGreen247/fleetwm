# Fleetwm

A minimal, fast Wayland window manager and desktop shell for Debian and
Ubuntu/Kubuntu. Built on [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots)
in C++, with a thin GTK4 top bar and settings app. No file manager, no app
launcher, no bundled apps -- just tiling window management, a bar, and a
power/settings menu, aimed squarely at low idle resource usage and
uncompromised gaming performance.

## Status

Early development. See [docs/adr](docs/adr) for the design decisions
behind the architecture, and the project plan for the phased roadmap this
is being built against.

## Features (target v1 scope)

- Tiling window manager (master-stack layout + floating toggle), per-output
  workspaces 1-9/0 with persistent per-workspace layout state
- Top bar: workspace switcher, live clock (`yyyy-mm-dd hh:mm:ss`), volume,
  CPU/GPU/disk usage
- Settings app: rounded/sharp corner toggle, 5 themes (Dark, Catppuccin,
  Dracula, OLED Black, Light), custom or wallpaper-auto-extracted accent
  color
- Settings app -- Bar tab: workspace-button corner shape, per-workspace
  colors; Wallpaper tab: image or solid-color background
  (`fleetwm-wallpaper`), applied live
- Battery indicator and power-profile switching (Normal/Performance/
  Battery Saver) in the bar and Settings, shown only on machines with a
  battery
- Settings app -- Default Apps: per-category default application picker
  (browser, terminal, file manager, image viewer, etc.), backed by
  standard `xdg-mime`/`mimeapps.list` associations so OS-level and other
  XDG-aware apps see the same defaults fleetwm sets
- Settings app -- About: hardware summary (CPU, GPU, RAM, disk, kernel)
  and project info (fleetwm version, license, links), in the spirit of
  KDE's/XFCE's/Windows' "About This System" pages
- Power menu: Settings, Sleep, Reboot, Poweroff
- App launcher (`fleetwm-launcher`, `Alt+D`): minimal Albert/dmenu-style
  popup, fuzzy search over installed applications (via `GDesktopAppInfo`)
  with a category hint per result, plus a "Run Command" fallback for
  typed shell commands
- XWayland support for legacy X11 apps
- `fleetwm-update`: pulls and rebuilds the latest version in place

## Requirements

- Debian 13.6 (Trixie) or Ubuntu/Kubuntu 26.04 LTS
- A display manager that lists Wayland sessions (GDM, SDDM, LightDM all
  work) -- or the bundled graphical `fleetwm-greet` login screen, see
  [Greeter](#greeter) below

## Install

```sh
git clone <this-repo-url> fleetwm
cd fleetwm
./install.sh
```

This installs build dependencies via `apt`, builds with Meson/Ninja, and
installs:

- `fleetwm`, `fleetwm-bar`, `fleetwm-settings`, `fleetwm-launcher`,
  `fleetwm-wallpaper`, `fleetwm-greet`, `fleetwm-greeter-login` to
  `/usr/local/bin`
- A `Fleetwm` session entry to `/usr/share/wayland-sessions/` (log out and
  pick it from your display manager's session list)
- Default theme files and `theme.toml` to `/etc/xdg/fleetwm/`
- `seatd` (installed and enabled as a system service) -- the greeter's
  own login-screen compositor needs it for DRM/seat access before
  anyone's logged in, see [Greeter](#greeter)
- The `fleetwm-greet` PAM config and systemd unit (not enabled by
  default -- see [Greeter](#greeter))

It also adds your user to the `input`, `video`, `render`, `audio`, and
`plugdev` groups (whichever exist on your system) -- the compositor needs
these for keyboard/mouse (`input`), GPU/DRM (`video`/`render`), and audio
device access. **Log out and back in (or reboot) after installing**, so
this group membership actually takes effect -- if you skip this, input or
rendering can fail silently with no error message, since a
permission-denied device node just looks like "no device" to
libinput/wlroots.

Log out and select **Fleetwm** as your session to start using it.

## Greeter

`fleetwm-greet` is a themed graphical login screen bundled with fleetwm,
for anyone who'd rather not run a full display manager. It's a real,
minimal wlroots compositor in its own right (see
[ADR 0006](docs/adr/0006-custom-pam-tty-greeter-vs-display-manager.md)
for why a custom greeter exists at all instead of depending on a display
manager): a Windows-7-style picker of every real local account, each shown
as a square avatar tile, plus an "Other User" tile for typing an
arbitrary name. Picking a tile locks the username and asks only for a
password; the background, accent color, and corner style all match
whatever's currently set in `fleetwm-settings`. **Root login is refused
outright** -- it never appears in the picker and is rejected server-side
even if typed under "Other User" -- root is meant to stay a deliberate
terminal/rescue-shell login, not a routine desktop session.

It's installed but not enabled by default; `install.sh` prints the exact
commands to opt in on your main console (tty1):

```sh
sudo systemctl disable --now getty@tty1.service
sudo systemctl enable --now fleetwm-greeter@tty1.service
```

Swap `tty1` for another VT (e.g. `tty2`) if you'd rather leave your normal
login console untouched and just try the greeter alongside it.

Switch to that VT (`Ctrl+Alt+F2`) to see the login screen. Requires
`seatd` running (`install.sh` installs and enables it automatically) --
the greeter's compositor needs a seat backend to get DRM access before
anyone's logged in, and `seatd` is what arbitrates that without opening a
login session of its own (which would collide with the one PAM opens for
whoever actually logs in). Build with `-Dgreeter=false` to skip the
greeter (and its `libpam` dependency) entirely.

## Updating

```sh
fleetwm-update
```

Pulls the latest source, rebuilds, and reinstalls in place. Requires
having installed via `install.sh` (it tracks the source checkout path in
`/etc/fleetwm-source-path`).

## Configuration

Edit `~/.config/fleetwm/theme.toml` directly, or use the settings app
(open it from the power icon at the right edge of the bar):

```toml
corner_style = "rounded"   # "rounded" | "sharp"
theme = "dark"             # "dark" | "catppuccin" | "dracula" | "oled_black" | "light"
accent = "auto"            # "auto" | "#RRGGBB"
```

Changes made via the settings app apply live, without restarting the bar
or compositor.

## Default keybinds

| Keybind             | Action                                    |
|---------------------|--------------------------------------------|
| `Alt+Return`        | Spawn a terminal (Settings' Default Apps tab; `foot` by default) |
| `Alt+Shift+Return`  | Promote the focused window to master      |
| `Alt+D`             | Open the app launcher                     |
| `Alt+Shift+Q`       | Close the focused window                  |
| `Alt+H`/`J`/`K`/`L` | Focus window left/down/up/right (spatial, vim-style) |
| `Alt+Shift+P`       | Toggle always-on-top pinning               |
| `Alt+Shift+F`       | Toggle floating (opt out of tiling)       |
| `Alt+Shift+L`       | Lock the session                          |
| `Alt+Shift+S`       | Region-select screenshot, copied to clipboard |
| `Alt+Escape`        | Quit the compositor                       |
| `Super+1`..`0`      | Switch to workspace 1-9, 0                |

More tiling/focus keybinds land in Phase 1. `Alt`, not `Super`, is used
for the terminal/quit binds in Phase 0 since `Super` is often already
claimed by the host compositor during nested development testing.

The *key* half of every Alt+`<key>`/Alt+Shift+`<key>` bind above is
remappable via `~/.config/fleetwm/keybinds.toml` (the modifier itself
stays fixed). Each field takes an xkb key name (e.g. `"Return"`, `"d"`,
uppercase like `"Q"` for a Shift-combined bind), picked up live with no
restart needed:

```toml
terminal = "Return"       # + Shift = promote focused window to master
launcher = "d"
close_window = "Q"
toggle_pin = "P"
toggle_float = "F"
lock = "L"
screenshot = "S"
focus_left = "h"
focus_down = "j"
focus_up = "k"
focus_right = "l"
quit = "Escape"
```

## Building from source manually

```sh
meson setup build --prefix=/usr/local --buildtype=release
ninja -C build
sudo ninja -C build install
```

Build dependencies (apt package names):

```
build-essential meson ninja-build pkg-config
libwlroots-0.18-dev wayland-protocols libwayland-dev
libinput-dev libdrm-dev libxkbcommon-dev libpixman-1-dev
libegl1-mesa-dev libgles2-mesa-dev
libgtk-4-dev libgtk4-layer-shell-dev
libpipewire-0.3-dev
libpam0g-dev
xwayland
foot
```

Build with `-Dxwayland=false` to disable XWayland support and drop the
`libxcb-dev` dependency. Build with `-Dgreeter=false` to skip
`fleetwm-greet` and its `libpam` dependency. If you enable the greeter
manually (not via `install.sh`), also install and enable `seatd`
(`apt install seatd && systemctl enable --now seatd.service`) -- see
[Greeter](#greeter).

## Architecture

Eight processes, communicating over a Unix domain socket and a
signal/pidfile mechanism -- see [docs/adr](docs/adr) for the reasoning
behind each:

- **`fleetwm`** -- the wlroots-based compositor and window manager
- **`fleetwm-bar`** -- the always-resident GTK4 top bar
- **`fleetwm-settings`** -- the settings/power-menu app, spawned on demand
- **`fleetwm-launcher`** -- the app launcher popup, spawned on demand
  (`Alt+D`), exits after one launch/dismiss
- **`fleetwm-wallpaper`** -- the background renderer, autostarted with the
  compositor
- **`fleetwm-update`** -- the update script
- **`fleetwm-greet`** -- the optional graphical login greeter, an
  alternative to running a full display manager (see
  [Greeter](#greeter)); not part of the IPC socket/signal mechanism since
  it runs before any session exists -- it's a small wlroots compositor of
  its own that hands off to `fleetwm` on a successful login
- **`fleetwm-greeter-login`** -- the GTK4 login-screen UI
  `fleetwm-greet` spawns and talks to over a private socket; never runs
  outside of a `fleetwm-greet` session

## License

MIT
