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
at the next boot, or start it now (this replaces the login on that console):

```sh
sudo systemctl start fleetwm-greeter@tty1.service
```

It is not enabled when a display manager is already enabled, or when you run the installer with
`FLEETWM_NO_GREETER=1`. To turn it off and get the normal text login back:

```sh
sudo systemctl disable --now fleetwm-greeter@tty1.service
sudo systemctl enable --now getty@tty1.service
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
