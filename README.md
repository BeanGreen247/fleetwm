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
  color, Windows-style or Vim-style window navigation
- Settings app -- Default Apps: per-category default application picker
  (browser, terminal, file manager, image viewer, etc.), backed by
  standard `xdg-mime`/`mimeapps.list` associations so OS-level and other
  XDG-aware apps see the same defaults fleetwm sets
- Settings app -- About: hardware summary (CPU, GPU, RAM, disk, kernel)
  and project info (fleetwm version, license, links), in the spirit of
  KDE's/XFCE's/Windows' "About This System" pages
- Power menu: Settings, Sleep, Reboot, Poweroff
- XWayland support for legacy X11 apps
- `fleetwm-update`: pulls and rebuilds the latest version in place

## Requirements

- Debian 13.6 (Trixie) or Ubuntu/Kubuntu 26.04 LTS
- A display manager that lists Wayland sessions (GDM, SDDM, LightDM all
  work) -- or the bundled minimal `fleetwm-greet` TTY login, see
  [Greeter](#greeter) below

## Install

```sh
git clone <this-repo-url> fleetwm
cd fleetwm
./install.sh
```

This installs build dependencies via `apt`, builds with Meson/Ninja, and
installs:

- `fleetwm`, `fleetwm-bar`, `fleetwm-settings`, `fleetwm-greet` to
  `/usr/local/bin`
- A `Fleetwm` session entry to `/usr/share/wayland-sessions/` (log out and
  pick it from your display manager's session list)
- Default theme files and `theme.toml` to `/etc/xdg/fleetwm/`
- The `fleetwm-greet` PAM config and systemd unit (not enabled by
  default -- see [Greeter](#greeter))

Log out and select **Fleetwm** as your session to start using it.

## Greeter

`fleetwm-greet` is a minimal PAM+TTY login prompt bundled with fleetwm, for
anyone who'd rather not run a full display manager. It's installed but not
enabled by default; `install.sh` prints the exact commands to opt in on
your main console (tty1):

```sh
sudo systemctl disable --now getty@tty1.service
sudo systemctl enable --now fleetwm-greeter@tty1.service
```

Swap `tty1` for another VT (e.g. `tty2`) if you'd rather leave your normal
login console untouched and just try the greeter alongside it.

Switch to that VT (`Ctrl+Alt+F2`) to see the prompt. See
[ADR 0006](docs/adr/0006-custom-pam-tty-greeter-vs-display-manager.md) for
why this exists alongside the display-manager path rather than replacing
it. Build with `-Dgreeter=false` to skip it (and its `libpam` dependency)
entirely.

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
nav_mode = "windows"       # "windows" | "vim"
```

Changes made via the settings app apply live, without restarting the bar
or compositor.

## Default keybinds

| Keybind        | Action                          |
|----------------|----------------------------------|
| `Alt+Return`   | Spawn a terminal (`foot`)       |
| `Alt+Escape`   | Quit the compositor             |
| `Super+1`..`0` | Switch to workspace 1-9, 0      |

More tiling/focus keybinds land in Phase 1. `Alt`, not `Super`, is used
for the terminal/quit binds in Phase 0 since `Super` is often already
claimed by the host compositor during nested development testing.

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
`fleetwm-greet` and its `libpam` dependency.

## Architecture

Five processes, communicating over a Unix domain socket and a signal/pidfile
mechanism -- see [docs/adr](docs/adr) for the reasoning behind each:

- **`fleetwm`** -- the wlroots-based compositor and window manager
- **`fleetwm-bar`** -- the always-resident GTK4 top bar
- **`fleetwm-settings`** -- the settings/power-menu app, spawned on demand
- **`fleetwm-update`** -- the update script
- **`fleetwm-greet`** -- the optional minimal PAM+TTY login greeter, an
  alternative to running a full display manager (see
  [Greeter](#greeter)); not part of the IPC socket/signal mechanism since
  it runs before any session exists

## License

MIT
