# Remote desktop (fleetwm-remote)

A built-in VNC and RDP server. `fleetwm-remote` (source `src/fleetremote/`) is an ordinary Wayland client of the
compositor: it grabs the first output with `wlr-screencopy` (only while a viewer is connected, only the damaged part,
cursor included) and plays remote input back with `wlr-virtual-pointer` and `virtual-keyboard`. The VNC and RDP
protocols are implemented here, from the specifications; no xrdp, x11vnc, wayvnc, libvncserver or FreeRDP.

## Use

Settings -> Remote Desktop. Switching VNC or RDP on makes a new random password and starts the server; off closes
it, disconnects everyone and clears the password. Settings writes `~/.config/fleetwm/remote.toml` (mode 0600); the
daemon (started with the session, one per user, exits at once when nothing is on) watches that file with inotify.

| | VNC | RDP |
|---|---|---|
| Default port | 5900 | 3389 |
| Login | password only (8 characters, a protocol limit) | user name = the login name, password (16 characters) |
| Encryption switch | on: VeNCrypt (TLS, then the password); off (default): the classic VNC login, picture not encrypted | on (default): TLS; off: standard RDP security with no encryption |
| Picture | Raw, ZRLE, CopyRect, DesktopSize | 32 bit raw, or 16 bit run-length coded |

Both servers have a **Quality** slider (1-100), a **Bandwidth limit** slider (no limit, 32 KB/s ... 8 MB/s), a **Render resolution**
dropdown (the screen's own size, or shrunk to fit 1920x1080 ... 480x270 with the proportions kept, never enlarged) and a **Colour depth**
dropdown (VNC: automatic, 8, 16, 24 bit; RDP: automatic, 16, 32 bit). Quality, limit and VNC resolution and depth apply at once to
connected viewers; changing the encryption switch, port or password, or the RDP resolution or depth, reconnects them.

## What the bar and Settings show

While a viewer is connected the bar shows a pill with the protocol; hover for viewers, bytes sent and received, current speed, CPU and
memory per protocol. The daemon writes these every 10 s and at every change. Settings -> Remote Desktop adds today's tally and the
per-day averages over 7 and 28 days, and the all-time total (kept in `~/.local/state/fleetwm/remote-usage.txt`, 60 days of days).

Apache Guacamole: protocol VNC (host, port, password) or RDP (host, port, user, password, security `tls` or `any`,
"ignore server certificate"). Windows Remote Desktop works when it is allowed to fall back from NLA. FreeRDP:
`xfreerdp3 /v:host:3389 /u:USER /p:PASSWORD /cert:ignore /sec:tls` (`/sec:rdp` with encryption off). TigerVNC with
encryption on: `xtigervncviewer host::5900 -SecurityTypes VeNCrypt,X509Vnc`. The certificate is self-signed
(`~/.config/fleetwm/remote/`), so viewers ask once whether to trust it.

## Low bandwidth

- **Quality** drops the low bits of every colour channel (100-85: none; 84-70: 1; 69-50: 2; 49-30: 3; below: 4), which
  makes flat areas and gradients compress much better, and sets the shortest time between updates (25, 50, 100, 200,
  400 ms). RDP also switches from 32 bit raw to 16 bit run-length coding below quality 90 or with any limit set.
- **Bandwidth limit** paces the sends so a viewer never averages more than the limit; when a viewer uses all of it
  the colour depth drops one more step (up to three) and comes back when there is room. One update is never split, so a
  limit below the size of one update is overshot by that much (measured: 64 KB/s gave 76 KB/s).
- The link's own speed is learned from how long the sends take, so a viewer on a slower link than the limit (or with
  no limit) is not flooded.
- Always on: a tile that came out identical (the compositor's damage is coarse) is not sent again; VNC sends a
  scroll as CopyRect (a few bytes) when it finds the new picture equal to the old one moved by some lines; ZRLE
  uses solid and palette tiles and a deflate stream per viewer.

For the internet: switch encryption on, use a strong port forward or an SSH tunnel/VPN, quality 30-60 and a limit.

## Security

Five failed logins in a minute block logins for the rest of the minute; changing the password or switching a server off
disconnects its viewers. Without TLS the VNC picture and the RDP traffic are readable on the wire and the VNC login is
the old challenge-response: use that only on a trusted LAN or through a tunnel. Open the port in the firewall by hand
(`ufw allow 5900/tcp`, `ufw allow 3389/tcp`).

## Not there

NLA/CredSSP, clipboard, sound, drive and printer redirection, multiple monitors (the first one is shown), cursor
shape updates, RDP scroll orders and codecs beyond 16 bit run-length coding, resizing the screen from the viewer, one
encode shared between several viewers (each viewer is encoded on its own, which only costs with more than one).

## Code

`src/fleetremote/`: `capture.*` (Wayland), `hub.*` (shared frame, per-viewer dirty 64x64 tiles, input queue),
`shadow.hpp` (what the viewer has: unchanged tiles, scroll detection), `throttle.hpp` (quality, limit, link speed),
`conn.hpp` (socket plus TLS), `tls.*`, `vnc.cpp`, `rdp.cpp`, `main.cpp` (listeners, inotify config watch, status file
`$XDG_RUNTIME_DIR/fleetwm-remote.status`). One thread per viewer. `FLEETWM_REMOTE_STATS=1` prints each viewer's bytes
when it leaves. Settings page: `apps/settings/remote_tab.hpp`; config: `src/common/remote_config.*`.
