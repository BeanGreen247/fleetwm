# VM scene scripts (2026-10-20)

Headless Fleetwm scenes used to check the Windows 7 look, the glass backdrop and the installed build. They never touch a real session
(own runtime dir, `WLR_BACKENDS=headless`, pixman). Copy the files to `/tmp` on the machine (`wp7.png` is a generated wallpaper with light
streaks; it must be at `/tmp/wp7.png`), then:

- `glass-scene.sh BUILD OUT.png [true|false]`: compositor + bar + three `foot` windows + `grim` screenshot. `BUILD` is a build directory
  (`.../b`) or the word `installed`. `BAR_EXTRA="taskbar_workspaces = 1"` appends a line to bar.toml.
- `glass-drag.sh BUILD [true|false]`: drags the front window over two others with `FLEETWM_GRAB_STATS=1` and prints `glass_builds` / `glass_ms`.
- `inst-settings-shots.sh`: installed compositor at 1024x768, Settings pages by pointer click (Theme, Taskbar), for the minimum-resolution check.
- `nftest.sh URI`: mounts a server with `fleetwm-netfs` and runs create / read / rename / mkdir / 20 MB compare / delete (needs vsftpd, sshd,
  Apache mod_dav or Samba set up as in docs/FILE_MANAGER.md; password `dev` on stdin).
Needs `foot`, `grim`, `wtype`, `dbus-run-session`; the drag script builds its pointer helper from `scripts/pgo-pointer.c`.
After syncing sources to a build machine with tar, `touch` them: older file times make ninja skip the rebuild.
