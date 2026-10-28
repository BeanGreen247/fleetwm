#!/usr/bin/env bash
# Heroic Games Launcher (Electron) starts on X11 by itself, and its window stops redrawing after a resize there. Give the user's copy of its
# desktop entry --ozone-platform=wayland so it runs natively; a system entry is never edited, and a user entry already there is left alone.
set -u
src=""
for f in /usr/share/applications/com.heroicgameslauncher.hgl.desktop /usr/share/applications/heroic.desktop; do
  [[ -f $f ]] && { src=$f; break; }
done
[[ -n $src ]] || exit 0
dest="${XDG_DATA_HOME:-$HOME/.local/share}/applications/$(basename "$src")"
[[ -e $dest ]] && exit 0
mkdir -p "$(dirname "$dest")"
sed -E 's|^(Exec=[^ ]*heroic)( )|\1 --ozone-platform=wayland\2|; s|^(Exec=[^ ]*heroic)$|\1 --ozone-platform=wayland|' "$src" > "$dest"
echo "fleetwm: $dest runs Heroic on Wayland"
