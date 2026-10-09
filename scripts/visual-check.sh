#!/bin/bash
# Inside the train container: headless compositor from /w/build-dev, then run the scenario script given as $1
# (sourced; it can use: cfg NAME 'toml', run PROG ARGS, shot FILE, sleepx S, ipc CMD) and leave PNGs in /w/shots.
BUILD=/w/build-dev
export XDG_RUNTIME_DIR=/tmp/rt HOME=/tmp/h WLR_BACKENDS=headless WLR_RENDERER=pixman LANG=C.UTF-8 LC_ALL=C.UTF-8
rm -rf /tmp/rt /tmp/h; mkdir -p /tmp/rt /tmp/h/.config/fleetwm /w/shots; chmod 700 /tmp/rt
export PATH=$BUILD/src/bar:$BUILD/apps/settings:$BUILD/apps/launcher:$BUILD/src/wallpaper:$BUILD/apps/audiomixer:$BUILD/apps/powermenu:$BUILD/apps/fleetfm:$BUILD/apps/desktop:$BUILD/apps/shortcuts:$BUILD/apps/langpicker:$BUILD/apps/ctxmenu:$PATH
cat > /tmp/inner.sh <<'IN'
CONF=$HOME/.config/fleetwm
cfg() { printf '%s\n' "$2" > $CONF/$1.tmp && mv $CONF/$1.tmp $CONF/$1; sleep ${3:-0.6}; }
run() { "$@" > /tmp/rt/$(basename $1).log 2>&1 & echo $! >> /tmp/pids; }
shot() { sleep ${2:-0.5}; grim /w/shots/$1; }
sleepx() { sleep $1; }
ipc() { python3 - /tmp/rt/fleetwm.sock "$1" <<'PY'
import socket,sys,time
s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);s.settimeout(0.5);s.connect(sys.argv[1]);s.sendall((sys.argv[1+1]+"\n").encode());time.sleep(0.05)
try: print(s.recv(65536).decode(errors="replace").strip()[:3000])
except Exception: pass
PY
}
: > /tmp/pids
/w/build-dev/src/compositor/fleetwm > /tmp/rt/comp.log 2>&1 & echo $! >> /tmp/pids
for i in $(seq 1 100); do [ -S /tmp/rt/fleetwm.sock ] && break; sleep 0.1; done
export WAYLAND_DISPLAY=wayland-0
source "$SCENARIO"
for p in $(tac /tmp/pids); do kill -TERM $p 2>/dev/null; done
sleep 0.5
tail -5 /tmp/rt/comp.log
IN
SCENARIO="$1" dbus-run-session -- bash /tmp/inner.sh
