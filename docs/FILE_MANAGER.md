# File manager (`fleetwm-fm`)

A native C++ file manager for Fleetwm. Windows 7 Explorer is the model (layout, behaviour, colours), with the look and the
behaviour switchable to Windows 10, Mac Finder, Caja (MATE), Nautilus (GNOME), Nemo (Cinnamon), Thunar (Xfce), PCManFM (LXDE) and
Dolphin (KDE). Drawn with cairo through fleetkit like the other Fleetwm programs: no GTK, no Qt, no GLib in the process.

Written 2026-10-09. What was measured is in `docs/FILE_MANAGER_PERFORMANCE.md`; what is still open is at the end of this file.

```
fleetwm-fm [FOLDER|URI ...]              each argument opens in a tab
fleetwm-fm --screenshot out.png [--size 1024x768] [--style windows7|windows10|mac|caja|nautilus|nemo|thunar|pcmanfm|dolphin]
                                [--view details|list|small_icons|medium_icons|large_icons|extra_large_icons|tiles|content]
                                [--show settings|about|properties|connect|nextcloud|menu-view|menu-organize|menu-context|tabs|select] [FOLDER]
fleetwm-fm --write-icon out.png [SIZE]   the application icon as a PNG
fleetwm-fm --version | --help            with the author credit and the project link, like every Fleetwm program
```

`--screenshot` renders one frame without a compositor (the window has no Wayland dependency), which is how the layouts were checked at
the 1024x768 minimum and how the unit tests drive it.

## What it does

**Browsing.** Tabs (Ctrl+T, Ctrl+W, Ctrl+Shift+T, Ctrl+Tab, middle-click a folder), back / forward / up, a breadcrumb address bar you can
click, drop down (the arrow after a crumb lists the sibling folders) or type into (Ctrl+L, F4, Alt+D), search that runs as you type
(subfolders, partial matches, `*` and `?`, optionally inside text files; cancelled and restarted on every letter), type-to-select,
rubber-band selection, check boxes, column sorting, resizable columns, per-folder view memory, all eight Explorer view modes (Ctrl+1..8,
Ctrl+wheel), picture thumbnails decoded on a worker thread (PNG, JPEG, WebP, SVG), live refresh through inotify.

**Computer / This PC.** Drives as tiles with a free-space bar that turns red above 90 %: internal disks, removable drives (USB sticks, SD
cards, optical), network mounts, and removable partitions that are plugged in but not mounted (double-click mounts them through udisks,
no root needed). Windows 10 style grouping ("Devices and drives", "Network locations") in the Windows 10, Mac and Linux styles.

**Copy and move with an integrity check** (`src/fleetfm/transfer.cpp`). Each file is written under a hidden temporary name
(`.name.fleetfm-part`), `fallocate`d up front (a full disk fails before the copy, not at the end), `fsync`ed to the device, then read
back from the device with `O_DIRECT` (the page cache cannot answer for the data; where the file system refuses `O_DIRECT` it drops the
cache and reads normally), hashed (SHA-256 by default, SHA-1 or MD5 on request) and compared with the hash taken from the source
while it was read. Only a match is renamed into place. A mismatch deletes the copy and reports it. A move deletes the original only after
that check passed, and is a plain `rename` when source and destination share a file system. Cancel leaves no `.part` file. Conflicts ask
(Replace, Skip, Keep both, "do this for all"), folders merge, a copy pasted into its own folder becomes "name - Copy.ext", a folder cannot
be copied into itself, symlinks stay symlinks, mode and modification time are kept. The progress card is the Windows 7 dialog: green
bar, "Copying 12 items - 41% complete", name, time remaining, speed, items remaining, Pause and Cancel, "More details" for the speed
graph. When verification is on the bar covers both passes and the card says "Checking 'x' by reading it back from the drive". Settings:
verify always / only on USB, cards and network places / never; checksum; flush mode; buffer size; direct read-back on or off. With
verification off the kernel does the copy (`copy_file_range`, reflink where the file system has it).

**Safe removal.** Eject (toolbar, context menu, the button on the drive in the navigation pane) does, in this order: refuse if a
transfer to or from the drive is still running; refuse and list the programs holding it open (`/proc/*/fd`, working directories); `syncfs`;
unmount through `udisksctl` (then `umount` if that fails for a reason other than "busy"); power the disk off when it is removable and no
other partition of it is mounted (`udisksctl power-off`, or `eject` for optical drives); then the Windows wording "The 'X' device can now
be safely removed from the computer." Nothing is forced; the system volumes (`/`, `/boot`, `/home` ...) cannot be ejected from here. This
window leaves the drive before unmounting, so it is not what holds it.

**Network places** (`src/fleetfm/locations.cpp`). SMB (Samba, NAS), SFTP/SSH, FTP and FTPS, WebDAV (`dav://`, `davs://`) including
Nextcloud and ownCloud (the dialog builds `davs://user@host/remote.php/dav/files/user/` from "cloud.example.com" and a user name), NFS,
AFP, MTP phones, iPhone (AFC), cameras, archives and administrator access, all mounted through GVfs (`gio mount`), the layer Nautilus, Nemo,
Caja and Thunar use. A mounted place is an ordinary folder below `/run/user/<uid>/gvfs/`, so listing, copying, verifying, searching and
thumbnails need no second code path. The address bar understands `smb://nas/media`, `\\nas\media` and `//nas/media`. Passwords go to `gio`
on its standard input and are never put on a command line, in a title, in `fleetfm-places.toml` or in a log. Saved places appear under
Network (Windows 10's "Network locations") and mounted ones under Computer with an eject button.

**Trash.** The freedesktop.org specification, so files trashed here appear in every other file manager and the other way round. Same
file system: `$XDG_DATA_HOME/Trash`; another drive: `<drive>/.Trash-<uid>` (a rename, so trashing from a USB stick is instant and the file
stays on the stick). Restore refuses to overwrite. If a file cannot be trashed the window offers permanent deletion.

**Settings** (Ctrl+, or Organize > Folder and search options; every change is saved at once to `~/.config/fleetwm/fleetfm.toml`).
General (style, open folders in same window / own window / new tab, single or double click, navigation pane contents, start location,
reopen tabs), View (default view, hidden files, extensions, full path in the title, check boxes, thumbnails and their size limit,
per-folder views, stripes, compact rows, type-to-select or type-to-search, details pane, status bar, menu bar never / Alt / always, sorting,
size and date formats, which columns), Tabs, Search, Copy and verify, Drives and network, Appearance (colour scheme: the style's own, the
Fleetwm theme, light, dark; font, row, icon and navigation-pane sizes), Privacy (history length, clear history). The file lists every
option with its key.

**Styles.** A style is data (`src/fleetfm/view_style.cpp`): where the toolbar and address bar sit, whether there is a menu bar, a navigation
tree (Windows) or a flat Places / Devices / Network sidebar (the rest), a details pane or a status bar, row height, how a selection is painted
(Win7 glass, flat, rounded accent, outline). Changing style also changes the view mode (Windows 7 Details, the Linux managers and Finder
icons), unless "A style also switches the view mode" is off.

**Icons are drawn in code** (`src/fleetfm/icons.cpp`): the application icon (a blue glass folder with a green check disc: similar spirit to
Windows 7's, deliberately not its artwork), 37 file, drive and place icons, and the link and lock badges. No image files are read; the
installed PNGs for launchers are written by the program itself (`scripts/install-fm-icons.py`).

**About** (Help menu, press Alt, or F1): version, "Copyright (c) 2026 Thomas Mozdren", the project link, MIT licence; `--version` and
`--help` print the same credit like every Fleetwm program.

## Keys

| | |
|---|---|
| Ctrl+C / X / V | copy, cut, paste (inside this program; file URIs pasted as text from other programs also work) |
| Ctrl+A, Ctrl+I | select all, invert |
| F2, Delete, Shift+Delete | rename, move to Trash (asks unless turned off), delete permanently |
| Ctrl+Shift+N | new folder, ready to rename |
| Alt+Left / Right / Up, Backspace | back, forward, up, back |
| Ctrl+L, F4 | edit the address |
| Ctrl+F, Ctrl+E, F3 | search |
| F5, Ctrl+R | refresh |
| Ctrl+H | hidden files |
| Ctrl+T, Ctrl+W, Ctrl+Shift+T, Ctrl+Tab | tabs |
| Ctrl+1 ... Ctrl+8 | view modes |
| Alt+Enter | properties (size of a folder is counted in the background; checksums on request) |
| F10, Alt | menu bar |
| F1 | about |

## Architecture

| Part | Files | Needs |
|---|---|---|
| Core (no window, no cairo) | `src/fleetfm/{transfer,hasher,mounts,locations,trash,search,dir_listing,natural_sort,format,speed_meter,browser,view_metrics,view_style,fm_settings,line_edit,process}.cpp` | POSIX, libcrypto, toml++ |
| Icons, file operations, navigation tree | `icons.cpp`, `file_ops.cpp`, `nav_tree.cpp` | + cairo |
| Window | `window*.cpp`, `ui_colors.cpp` | + fleetkit (cairo text, the immediate-mode widgets used by the dialogs) |
| Program | `apps/fleetfm/main.cpp` | + Wayland (fleetkit) |

The window takes input as calls (`on_key`, `on_button`, ...) and draws into any cairo context; `Host` carries what only the program can do
(redraw, post to the main thread, timers, inotify, clipboard text). That is why the 200 unit tests drive real clicks and key presses
headless. Work that can block runs off the main thread: reading a folder, searching, measuring a folder, thumbnails, copying, ejecting,
mounting, listing volumes (a `statvfs` on a dead NFS mount cannot freeze the window).

Tests: `scripts/test.sh fm` (area `fm`, `Fm*`), 200+ cases: format and natural sort, listing, the copy engine (conflicts, merge, move,
cancel, pause, no `.part` left behind), mountinfo and sysfs fixtures, eject ordering with a fake runner, URIs and GVfs names, trash
round trips, search, view geometry and hit testing, every style at 1024x768, every dialog, scripted mouse and keyboard sessions.
Also run clean under AddressSanitizer and UndefinedBehaviorSanitizer (the only report is fontconfig's own leak) and ThreadSanitizer.

## Protocols: what other Linux file managers use

Checked on this machine (`/usr/lib/gvfs`): GVfs ships backends `smb`, `smb-browse`, `sftp`, `ftp`, `dav`, `http`, `nfs`, `afp`,
`afp-browse`, `afc`, `mtp`, `gphoto2`, `google`, `onedrive`, `archive`, `cdda`, `dnssd`, `wsdd`, `admin`, `computer`, `network`, `recent`, `trash`, plus
`gvfsd-fuse` which exposes mounts as ordinary folders. The GVfs README (fetched 2026-10-09) describes it as "a userspace virtual
filesystem implementation for GIO ... trash support, SFTP, SMB, HTTP, DAV, and many others ... FUSE support that provides limited access
to the GVfs filesystems for applications not using GIO". Nautilus, Nemo, Caja and Thunar sit on it; PCManFM reaches the same backends
through libfm; Dolphin uses KDE's KIO workers for the same protocols plus `fish://`. The Connect dialog and the address bar cover the
GVfs set.

## Added after the first version (2026-10-09, same day)
- **Clipboard to and from other programs.** Copy and cut put `x-special/gnome-copied-files`, `text/uri-list` and plain text on the Wayland clipboard
  (`App::set_clipboard`), and paste reads them, so files move between this program, Nautilus, Nemo, Caja, Thunar and the rest. "Copy location" puts the path as text.
- **Drag and drop**, in the window and between programs (`wl_data_device`: source and target in fleetkit). **A plain drop asks what to do: Copy here
  (first and already highlighted, Enter takes it), Move here, Create symbolic link here, Create hard link here, Cancel.** Ctrl copies, Shift moves and
  Alt links at once without asking. Folder items, navigation rows, tabs and address crumbs are drop targets and light up. A hard link to a folder or across
  drives says why it cannot be made. Without compositor drag and drop the window drags by itself (ghost label under the pointer).
- **The right-click menus are Windows 7 menus** (icon gutter, bold default item, submenus) with the extras of the Linux managers: Open, Open in new tab / new
  window / terminal, Open with > (the programs the mime type is associated with), Send to > (Desktop, Documents, Downloads, removable drives), Cut, Copy, Copy
  location, Paste into folder, Create shortcut, Delete, Rename, Compress to > (.zip, .tar.gz, .tar.xz), Extract > (here, to a folder), Calculate checksums, Properties.
  On empty space: View >, Sort by >, Group by >, Refresh, Paste, Undo, New > (Folder, Text document), Open in terminal, Properties.
- **Group by** (Name, Type, Size, Date modified) in the Details view, with headings and counts, in the View menu, the Sort by menu and Settings.
- **Preview pane** (Organize > Preview pane): a picture at the size of the pane, the first lines of a text file, or the icon and facts. **Tooltips** after 0.6 s.
- **Undo** (Ctrl+Z, Organize and the empty-space menu): rename, new folder or document, delete (restores from the trash), copy (the copies go to the trash), move (moves
  back), links, compress and extract.
- **Computers on the network** are listed under Network (`gio list network://`, GVfs's mDNS and WS-Discovery), next to the saved places.
- **`fleetwm-fm --train DIR`** drives the whole window with scripted input on a tree it makes in DIR (nothing outside it is touched) for the PGO training run and as a quick
  smoke test (1.4 s).

## Not done, or not verified here
1. **Network protocols were not run against real servers.** The VM has no Samba, SSH, FTP or Nextcloud server. The `gio mount` call, the
   prompt answers (user, domain, password on stdin), the GVfs folder names, the Nextcloud address and the network scan are unit-tested against a fake runner
   and fixtures only. First real use may need adjustment (for example the prompt order of `gio mount` for a given backend).
2. **No real removable drive was available.** Mount-table parsing, sysfs classification and the eject sequence are tested with fixtures and a fake `udisksctl`.
3. **Native FTP, SFTP, WebDAV and SMB clients are not written**; everything network goes through GVfs. They would need a virtual file system layer under listing,
   copying, verifying, searching and thumbnails (all of which now read plain paths, which is why GVfs's FUSE folders work for free); that is a rewrite of the data path, not an addition.
4. **Clipboard and drag and drop were only exercised through the unit tests and a fake host**; the Wayland side (`wl_data_source` and `wl_data_offer`) compiles and runs but was never
   driven by a second program here (no other Wayland file manager is installed). Opening with a chosen program and compress/extract need the tools (`zip`, `unzip`, `tar`, `7z`) installed.
5. Group by applies to the Details view only; the icon views list items without headings. No "Other application..." chooser under Open with.
6. The pointer was never moved by a real mouse in a running compositor session here.
