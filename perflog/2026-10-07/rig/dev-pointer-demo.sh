#!/bin/bash
B=/w/srcT/build-pgo
RT=$(mktemp -d /tmp/dev.XXXXXX); chmod 700 $RT
export XDG_RUNTIME_DIR=$RT HOME=$RT WLR_BACKENDS=headless WLR_RENDERER=pixman LANG=C.UTF-8 LC_ALL=C.UTF-8
export PATH=$RT/bin:$PATH
bash /w/srcT/scripts/pgo-path-shim.sh $B $RT/bin
mkdir -p $HOME/.config/fleetwm
printf 'window_layout = "desktop"\nglass_effects = true\ntheme = "dark"\n' > $HOME/.config/fleetwm/theme.toml
$B/src/compositor/fleetwm > $RT/comp.log 2>&1 &
CP=$!
for i in $(seq 1 50); do [ -S $RT/fleetwm.sock ] && break; sleep 0.2; done
export WAYLAND_DISPLAY=wayland-0
P=/w/ptest/pgo-pointer
$B/apps/settings/fleetwm-settings --page theme >/dev/null 2>&1 & SP=$!
sleep 2
grim /w/shots/a0.png
$P drag 645 70 500 200 10; sleep 0.3; grim /w/shots/a1_moved.png
$P drag 903 400 1000 400 8; sleep 0.3; grim /w/shots/a2_resized.png
$P dclick 500 200; sleep 0.5; grim /w/shots/a3_dclick.png
$P dclick 640 12; sleep 0.5; grim /w/shots/a4_dclick2.png
kill -TERM $SP $CP 2>/dev/null; wait 2>/dev/null
