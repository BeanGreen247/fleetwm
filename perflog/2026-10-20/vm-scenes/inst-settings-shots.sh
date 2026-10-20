#!/bin/bash
# installed compositor, headless: (1) chromium launches x10 with Wayland, count crash dumps and whether a window mapped; (2) Settings at 1024x768 Taskbar tab screenshot
export XDG_RUNTIME_DIR=/tmp/rt-it HOME=/tmp/h-it WLR_BACKENDS=headless WLR_RENDERER=pixman LANG=C.UTF-8 LC_ALL=C.UTF-8
rm -rf $XDG_RUNTIME_DIR $HOME; mkdir -p $XDG_RUNTIME_DIR $HOME/.config/fleetwm; chmod 700 $XDG_RUNTIME_DIR
printf "theme = 'dark'\nwindow_layout = 'desktop'\nglass_effects = true\n" > $HOME/.config/fleetwm/theme.toml
printf "path = '/tmp/wp7.png'\nuse_solid_color = false\n" > $HOME/.config/fleetwm/wallpaper.toml
cat > /tmp/it-inner.sh <<'IN'
/usr/local/bin/fleetwm > /tmp/rt-it/comp.log 2>&1 &
CP=$!
for i in $(seq 1 100); do [ -S /tmp/rt-it/fleetwm.sock ] && break; sleep 0.1; done
export WAYLAND_DISPLAY=wayland-0
sleep 3
python3 - <<'PY'
import socket,time
s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM); s.connect('/tmp/rt-it/fleetwm.sock')
s.sendall(b"OUTPUT_SET HEADLESS-1 1024 768 60000 -999999 -999999\n"); time.sleep(0.5)
PY
sleep 1
foot sh -c 'sleep 1' & sleep 0.5
fleetwm-settings > /tmp/rt-it/settings.log 2>&1 &
SP=$!; sleep 3
# the Taskbar tab: tabs by keyboard (Ctrl+Tab / Tab navigation differs per build); take a shot of the first page and of a later page
export PGO_W=1024 PGO_H=768
ls /tmp/pgo-pointer >/dev/null 2>&1 && /tmp/pgo-pointer click 160 241 2>>/tmp/ptr2.err; sleep 1; grim /tmp/it-settings-taskbar.png
/tmp/pgo-pointer click 160 203 2>>/tmp/ptr2.err; sleep 1; grim /tmp/it-settings-theme.png
kill $SP $CP 2>/dev/null
IN
dbus-run-session -- bash /tmp/it-inner.sh
echo "dumps in default dir: $(ls ~/.config/chromium 2>/dev/null | head -1)"; grep -ci 'segmentation\|SIGSEGV' /tmp/rt-it/comp.log
