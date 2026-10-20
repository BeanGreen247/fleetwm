#!/bin/bash
# Headless compositor with a Windows-7-like wallpaper, three terminals, then a grim screenshot. usage: glass-scene.sh BUILD_DIR OUT.png [glass true|false]
BUILD=$1; OUT=$2; GLASS=${3:-true}
COMP=$BUILD/src/compositor/fleetwm; [ "$BUILD" = installed ] && COMP=/usr/local/bin/fleetwm
export XDG_RUNTIME_DIR=/tmp/rt-gs HOME=/tmp/h-gs WLR_BACKENDS=headless WLR_RENDERER=pixman LANG=C.UTF-8 LC_ALL=C.UTF-8
rm -rf $XDG_RUNTIME_DIR $HOME; mkdir -p $XDG_RUNTIME_DIR $HOME/.config/fleetwm; chmod 700 $XDG_RUNTIME_DIR
cat > $HOME/.config/fleetwm/theme.toml <<T
theme = 'dark'
window_layout = 'desktop'
glass_effects = $GLASS
accent = '#005cf2'
[glass_tint]
color = '#4a86c8'
intensity = 30
T
printf "pinned = ['foot.desktop', 'org.gnome.Nautilus.desktop']\n" > $HOME/.config/fleetwm/bar.toml
[ -n "$BAR_EXTRA" ] && printf '%s\n' "$BAR_EXTRA" >> $HOME/.config/fleetwm/bar.toml
cat > $HOME/.config/fleetwm/wallpaper.toml <<T
path = '/tmp/wp7.png'
use_solid_color = false
T
[ "$BUILD" = installed ] || export PATH=$BUILD/src/bar:$BUILD/src/wallpaper:$BUILD/apps/desktop:$BUILD/apps/launcher:$BUILD/apps/settings:$BUILD/apps/fleetfm:$BUILD/apps/shortcuts:$PATH
cat > /tmp/gs-inner.sh <<'IN'
$COMP > /tmp/rt-gs/comp.log 2>&1 &
CP=$!
for i in $(seq 1 100); do [ -S /tmp/rt-gs/fleetwm.sock ] && break; sleep 0.1; done
export WAYLAND_DISPLAY=wayland-0
sleep 3
foot -T "Hamilton C shell x64" sh -c 'ls --color=always /usr/bin | head -40; sleep 60' &
sleep 0.8
foot -T "hmore whereis.csh" sh -c 'ls -l /etc | head -40; sleep 60' &
sleep 0.8
foot -T "Hamilton C shell x64" sh -c 'echo hello; ls /usr | head; sleep 60' &
sleep 3
grim $OUT
kill $CP
IN
COMP=$COMP OUT=$OUT dbus-run-session -- bash -c "export COMP=$COMP OUT=$OUT; $(cat /tmp/gs-inner.sh)"
tail -3 /tmp/rt-gs/comp.log
