# ADR 0006: Custom PAM+TTY greeter vs a display manager

## Status
Accepted

## Context
Fleetwm previously only launched via an existing display manager's
`.desktop` session entry (GDM/SDDM/LightDM), which pulls in a full DM
daemon, theming, and a session-picker UI fleetwm doesn't need -- it only
ever has one session to offer. `ly` demonstrates the shape of a lightweight
PAM+TTY greeter but is a separate project to depend on and package, and
carries more than fleetwm needs (multi-session picker, theming, TUI
chrome). wlroots + libseat already handle VT switching and DRM-master
acquisition automatically when the compositor is launched from a bare TTY
(`wlr_backend_autocreate()`, `src/compositor/server.cpp`) -- no manual VT
or seat code exists anywhere in this repo -- so the only missing piece for
a minimal TTY login path is authentication and session setup, not seat
management.

## Decision
Build a minimal in-tree `fleetwm-greet` binary (`src/greeter/`) that does
only: a raw-termios username/password prompt, PAM authenticate + open
session, privilege drop, and `execve` of `fleetwm`. It runs exclusively as
a root-owned systemd service, templated per-tty
(`fleetwm-greeter@.service`), conflicting with the corresponding
`getty@.service` -- never as a setuid-root binary. This coexists with, and
does not replace, the existing `packaging/fleetwm.desktop` display-manager
path; both remain supported, and installing/enabling the greeter is
opt-in (`install.sh` prints instructions rather than auto-enabling it).

## Consequences
- Introduces PAM as fleetwm's first dependency (`libpam0g-dev`; Debian has
  no `pam.pc`, so `cpp.find_library('pam')` is used instead of
  `dependency('pam')`).
- Introduces the project's first systemd unit file and first
  root-running process, expanding fleetwm's privilege/attack surface. This
  is mitigated by keeping the root-privileged code path to four small
  files (`main.cpp`, `tty.cpp`, `auth.cpp`, `session.cpp`), avoiding a
  setuid binary entirely (root privilege exists only for the systemd
  unit's process lifetime), and checking every return value of the
  `initgroups`/`setgid`/`setuid` privilege-drop sequence before any exec.
- A new `-Dgreeter` meson option (default `true`, mirroring the existing
  `xwayland` option) makes the whole feature, and its PAM dependency,
  skippable for anyone building without wanting it.
- Installing the greeter's PAM config and systemd unit is a manual
  `install.sh` step, not a meson `install_data` target, since dropping a
  getty-conflicting unit shouldn't happen unconditionally on every
  `ninja install`.
