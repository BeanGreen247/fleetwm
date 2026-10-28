#!/usr/bin/env bash
# In a Wayland session Heroic Games Launcher (Electron) starts as a native Wayland client, and there its game library stays blank and the window stops
# repainting after a resize. On X11 (Xwayland) it resizes and repaints correctly, so give the user's copy of its desktop entry --ozone-platform=x11. A system
# entry is never edited, and a user entry that already exists is left alone.
set -u
src=""
for f in /usr/share/applications/com.heroicgameslauncher.hgl.desktop /usr/share/applications/heroic.desktop; do
  [[ -f $f ]] && { src=$f; break; }
done
[[ -n $src ]] || exit 0
dest="${XDG_DATA_HOME:-$HOME/.local/share}/applications/$(basename "$src")"
[[ -e $dest ]] && exit 0
mkdir -p "$(dirname "$dest")"
sed -E 's|^(Exec=[^ ]*heroic)( )|\1 --ozone-platform=x11\2|; s|^(Exec=[^ ]*heroic)$|\1 --ozone-platform=x11|' "$src" > "$dest"
echo "fleetwm: $dest runs Heroic on X11"
