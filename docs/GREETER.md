# Login screen (fleetwm-greet)

`fleetwm-greet` is a themed graphical login screen bundled with fleetwm,
for anyone who'd rather not run a full display manager. It's a real,
minimal wlroots compositor in its own right (see
[ADR 0006](adr/0006-custom-pam-tty-greeter-vs-display-manager.md)
for why a custom greeter exists at all instead of depending on a display
manager): a Windows-7-style picker of every real local account, each shown
as a square avatar tile, plus an "Other User" tile for typing an
arbitrary name. Picking a tile locks the username and asks only for a
password; the background, accent color, and corner style all match
whatever's currently set in `fleetwm-settings`. **Root login is refused
outright** -- it never appears in the picker and is rejected server-side
even if typed under "Other User" -- root is meant to stay a deliberate
terminal/rescue-shell login, not a routine desktop session.

`install.sh` enables it on tty1 and sets the default boot target to graphical. It takes effect
at the next boot. Do not start it by hand while you are logged in: it takes the screen from the running session.

## Keyring

The login screen's PAM service (`packaging/fleetwm-greeter-pam.conf`, installed as `/etc/pam.d/fleetwm-greeter`) unlocks the login keyring with the password you type,
the way lightdm and GDM do: `pam_gnome_keyring.so` in the auth stack takes the password, and in the session stack (after `pam_systemd`) it starts the keyring daemon and
unlocks it, and puts its socket variables into the session's environment. Nothing asks for the keyring password after login, so what depends on it (ssh keys,
Chromium and other Secret Service clients, git credentials) finds it open. `install.sh` installs `gnome-keyring` and `libpam-gnome-keyring`; the lines start with `-`, so
a machine without the module simply skips them. It only works when the keyring's own password is the same as the login password (a keyring made with another password stays locked
and asks once: change it to the login password in Seahorse, "Passwords and Keys", right-click the "Login" keyring, "Change Password"). The unlock happens when you log in, not
before: nothing can read a password-protected keyring before its password is typed.

## Sessions

The login card lists every desktop installed on the machine, not only Fleetwm: the entries in
`wayland-sessions/` and `xsessions/` (`/usr/local/share` and `/usr/share`), so MATE, Xfce, KDE, GNOME, sway and
the like start from here too. With more than one session the card shows a picker under the password field: click the
left or right half, or press F2, to cycle. The last session that logged in is preselected
(`/var/lib/fleetwm-greeter/last-session`). The login screen sends only the session's id; the command always comes
from the `.desktop` file, found again by the root process. Entries with a missing `TryExec` or `Hidden=true` are left out.
Wayland sessions run their `Exec` through `sh -c`. X11 sessions need `xinit` and `Xorg` (the installer adds
`xinit` and `xauth`; without them X11 entries are not listed) and run `xinit /etc/X11/Xsession <Exec> -- Xorg :N vtN`
on the greeter's console. Environment (`XDG_SESSION_TYPE`, `XDG_CURRENT_DESKTOP` from `DesktopNames=`, `XDG_VTNR`) is set
per session; the Fleetwm-only variables stay Fleetwm-only. Not tested yet on real hardware: X11 sessions, and any
session other than Fleetwm.

The unit backs off (1 s up to 30 s) when it cannot get the screen, which is what happens while another display
manager still runs. Switch a machine over by disabling the old display manager first, then rebooting:
`sudo systemctl disable lightdm`, `sudo systemctl disable --now getty@tty1`, `sudo systemctl enable fleetwm-greeter@tty1`,
`sudo systemctl reboot`. Do not start the greeter on top of a running desktop (2026-10-26: 49 failed DRM-master grabs in a
minute, visible as glitching on the running X session).

The installer enables it on tty1 and, when a display manager (lightdm, gdm, sddm, greetd, ...) is enabled, disables that
one for the next boot. It does not stop it, so whoever runs the installer stays logged in; reboot to see the login
screen. The old one is disabled, not removed (`sudo systemctl enable lightdm` brings it back; the name is saved in
`/var/lib/fleetwm-greeter/replaced-display-manager`). `FLEETWM_KEEP_DM=1` keeps the display manager and skips the login
screen instead; `FLEETWM_NO_GREETER=1` skips the login screen. Never start the login screen by hand while a display
manager is still running (2026-10-26, PeakPulse: the clash made the screen glitch; disabling and stopping lightdm fixed it).

To go back to the normal login (at the next boot, nobody is logged out; add the `enable` line for your old display
manager if the installer disabled one):

```sh
sudo systemctl disable fleetwm-greeter@tty1.service
sudo systemctl enable getty@tty1.service
sudo systemctl enable lightdm.service   # only if it was disabled
sudo systemctl reboot
```

Set `FLEETWM_GREETER_TTY=tty2` when running the installer to use another console and leave tty1
alone.

Switch to that VT (`Ctrl+Alt+F2`) to see the login screen. Requires
`seatd` running (`install.sh` installs and enables it automatically) --
the greeter's compositor needs a seat backend to get DRM access before
anyone's logged in, and `seatd` is what arbitrates that without opening a
login session of its own (which would collide with the one PAM opens for
whoever actually logs in). Build with `-Dgreeter=false` to skip the
greeter (and its `libpam` dependency) entirely.
