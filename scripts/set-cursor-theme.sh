#!/usr/bin/env bash
# Points GTK 3 and GTK 4 at Fleetwm's own pointer (the xcursor theme the compositor writes at login, src/compositor/xcursor_theme.cpp), so every program shows the same cursor.
# Run by the installers for the user who installs; safe to run again. The first time a settings.ini is changed, the original is kept as settings.ini.fleetwm-bak.
#   scripts/set-cursor-theme.sh [THEME]    (default fleetwm-aero; fleetwm-flat is the version without glass)
set -euo pipefail
THEME="${1:-fleetwm-aero}"
SIZE=32
command -v python3 >/dev/null 2>&1 || { echo "set-cursor-theme: python3 not found, GTK cursor not set"; exit 0; }
for v in gtk-3.0 gtk-4.0; do
  python3 - "${XDG_CONFIG_HOME:-$HOME/.config}/$v/settings.ini" "$THEME" "$SIZE" <<'PY'
import os, shutil, sys
path, theme, size = sys.argv[1:4]
os.makedirs(os.path.dirname(path), exist_ok=True)
lines = open(path).read().split("\n") if os.path.exists(path) else []
if os.path.exists(path) and not os.path.exists(path + ".fleetwm-bak"):
    shutil.copy2(path, path + ".fleetwm-bak")
want = {"gtk-cursor-theme-name": theme, "gtk-cursor-theme-size": size}
if not any(l.strip() == "[Settings]" for l in lines):
    lines = ["[Settings]"] + [l for l in lines if l != ""]
done = set()
out = []
for l in lines:
    key = l.split("=", 1)[0].strip()
    if key in want and "=" in l:
        out.append(f"{key}={want[key]}")
        done.add(key)
    else:
        out.append(l)
for key, val in want.items():
    if key not in done:
        i = next(n for n, l in enumerate(out) if l.strip() == "[Settings]")
        out.insert(i + 1, f"{key}={val}")
text = "\n".join(out)
open(path, "w").write(text if text.endswith("\n") else text + "\n")
PY
done
echo "GTK cursor theme set to $THEME"
