#!/bin/bash
# Headless compositor + three foot windows, then drag the front window over the others for ~10 s with FLEETWM_GRAB_STATS; prints the stats lines.
BUILD=$1; GLASS=${2:-true}; ROOT=${3:-$HOME/fx}
export XDG_RUNTIME_DIR=/tmp/rt-gd HOME=/tmp/h-gd WLR_BACKENDS=headless WLR_RENDERER=pixman LANG=C.UTF-8 LC_ALL=C.UTF-8 FLEETWM_GRAB_STATS=1
rm -rf $XDG_RUNTIME_DIR $HOME; mkdir -p $XDG_RUNTIME_DIR $HOME/.config/fleetwm; chmod 700 $XDG_RUNTIME_DIR
printf "theme = 'dark'\nwindow_layout = 'desktop'\nglass_effects = $GLASS\n" > $HOME/.config/fleetwm/theme.toml
printf "path = '/tmp/wp7.png'\nuse_solid_color = false\n" > $HOME/.config/fleetwm/wallpaper.toml
PTR=/tmp/pgo-pointer
if [ ! -x $PTR ]; then d=$(mktemp -d); wayland-scanner client-header $ROOT/scripts/wlr-virtual-pointer-unstable-v1.xml $d/wlr-virtual-pointer-unstable-v1-client-protocol.h && wayland-scanner private-code $ROOT/scripts/wlr-virtual-pointer-unstable-v1.xml $d/code.c && cc -O1 -w -I$d -o $PTR $ROOT/scripts/pgo-pointer.c $d/code.c -lwayland-client; fi
export PATH=$BUILD/src/bar:$BUILD/src/wallpaper:$BUILD/apps/desktop:$PATH
cat > /tmp/gd-inner.sh <<'IN'
$BUILD/src/compositor/fleetwm > /tmp/rt-gd/comp.log 2>&1 &
CP=$!
for i in $(seq 1 100); do [ -S /tmp/rt-gd/fleetwm.sock ] && break; sleep 0.1; done
export WAYLAND_DISPLAY=wayland-0
sleep 3
foot -T "one" sh -c 'ls --color=always /usr/bin | head -60; sleep 90' & sleep 0.7
foot -T "two" sh -c 'ls -l /etc | head -60; sleep 90' & sleep 0.7
foot -T "front" sh -c 'echo hello; sleep 90' & sleep 2.5
export PGO_W=1280 PGO_H=720
for r in 1 2 3 4 5 6; do /tmp/pgo-pointer drag 560 62 330 150 40 2>>/tmp/ptr.err; sleep 0.4; /tmp/pgo-pointer drag 330 150 560 62 40 2>>/tmp/ptr.err; sleep 0.4; done
sleep 1.2
kill $CP
IN
BUILD=$BUILD dbus-run-session -- bash -c "export BUILD=$BUILD; $(cat /tmp/gd-inner.sh)"
grep GRABSTATS /tmp/rt-gd/comp.log | awk '{for(i=1;i<=NF;i++){split($i,a,"="); if(a[1]=="mode"||a[1]=="events"||a[1]=="commits"||a[1]=="commit_ms"||a[1]=="glass_builds"||a[1]=="glass_ms") printf "%s ", $i} printf "\n"}' | grep -v 'mode=idle' | head -20
