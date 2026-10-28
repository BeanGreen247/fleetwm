#!/usr/bin/env bash
# Run from the source tree: scripts/settings-tux-refresh-test.sh BUILD_DIR. Needs tuxthrottlectl, grim, dbus-run-session, Pillow.
# the Settings Gaming page re-reads TuxThrottle by itself: flip the fake readout and the page must change without a click
B=$(realpath "$1"); OUT=/tmp/tuxauto; mkdir -p $OUT
T=$(mktemp -d /tmp/fleetwm-tp.XXXXXX); SRC=$PWD
wayland-scanner client-header $SRC/scripts/wlr-virtual-pointer-unstable-v1.xml $T/wlr-virtual-pointer-unstable-v1-client-protocol.h; wayland-scanner private-code $SRC/scripts/wlr-virtual-pointer-unstable-v1.xml $T/wlr-virtual-pointer-unstable-v1-protocol.c
cc -O1 -I$T $SRC/scripts/pgo-pointer.c $T/wlr-virtual-pointer-unstable-v1-protocol.c -o $T/pointer $(pkg-config --cflags --libs wayland-client) || exit 1
RT=$(mktemp -d /tmp/fleetwm-ta.XXXXXX); chmod 700 $RT; mkdir -p $RT/home/.config/fleetwm $RT/fake
real=$(tuxthrottlectl status --json)
printf '%s' "$real" | sed 's/"game_mode": *[a-z]*/"game_mode": false/' > $RT/fake/off.json
printf '%s' "$real" | sed 's/"game_mode": *[a-z]*/"game_mode": true/' > $RT/fake/on.json
cp $RT/fake/off.json $RT/fake/cur.json
printf '#!/bin/sh\ncat %s/fake/cur.json\n' $RT > $RT/fake/tuxthrottlectl; chmod +x $RT/fake/tuxthrottlectl
printf 'window_layout = "desktop"\n' > $RT/home/.config/fleetwm/theme.toml
cat > $RT/inner.sh <<IN
export FLEETWM_GAMEMODE_TEST=1 XDG_RUNTIME_DIR=$RT HOME=$RT/home WLR_BACKENDS=headless WLR_RENDERER=pixman LANG=C.UTF-8 NO_AT_BRIDGE=1 T=$T PATH=$RT/fake:$B/src/bar:$B/apps/settings:\$PATH
$B/src/compositor/fleetwm > $RT/comp.log 2>&1 &
CP=\$!
for i in \$(seq 1 100); do [ -S $RT/wayland-0 ] && break; sleep 0.1; done
export WAYLAND_DISPLAY=wayland-0
fleetwm-settings --page gaming > /dev/null 2>&1 &
SP=\$!
sleep 3
$T/pointer abs 740 400 scroll 40 wait 200 scroll 40 wait 200 scroll 40 wait 200 scroll 40 wait 200 scroll 40 wait 200 scroll 40 wait 200 scroll 40 wait 200 scroll 40 wait 200 scroll 40 wait 500
sleep 1
grim $OUT/a.png
cp $RT/fake/on.json $RT/fake/cur.json
sleep 5
grim $OUT/b.png

kill \$SP; pkill -TERM -P \$CP 2>/dev/null; kill -TERM \$CP 2>/dev/null; sleep 0.5
IN
dbus-run-session -- bash $RT/inner.sh >/dev/null 2>&1
python3 - <<'PY'
from PIL import Image, ImageChops
a, b = Image.open("/tmp/tuxauto/a.png").convert("RGB"), Image.open("/tmp/tuxauto/b.png").convert("RGB")
d = ImageChops.difference(a, b).getbbox()
print("PASS: the page changed by itself (%s)" % (d,) if d else "FAIL: the page did not change")
PY
rm -rf $RT
