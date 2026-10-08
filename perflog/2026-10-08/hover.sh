#!/bin/bash
# hover.sh SECS : sweep a virtual pointer across the bottom taskbar in a nested headless compositor; CPU of bar and compositor idle vs sweep
T=${1:-30}; P=/home/dev/perf-test
d=/tmp/ptr; mkdir -p $d
wayland-scanner client-header /tmp/wlr-virtual-pointer-unstable-v1.xml $d/wlr-virtual-pointer-unstable-v1-client-protocol.h && wayland-scanner private-code /tmp/wlr-virtual-pointer-unstable-v1.xml $d/c.c && cc -O1 -w -I$d -o $d/ptr /tmp/pgo-pointer.c $d/c.c -lwayland-client 2>&1 | tail -3
pkill -u dev -x fleetwm-bar; pkill -u dev -x fleetwm; sleep 1
bash $P/nest_headless.sh P >/dev/null 2>&1; sleep 4
RT=/tmp/ptest-run-P; HM=/tmp/ptest-home-P
export XDG_RUNTIME_DIR=$RT WAYLAND_DISPLAY=wayland-0
CP=$(cat $RT/pid); BP=$(pgrep -x fleetwm-bar | head -1)
cpu(){ awk '{print int($1/1000000)}' /proc/$1/schedstat; }
cmds=""; for r in 1 2 3 4 5 6 7 8 9 10; do for x in $(seq 20 40 1260); do cmds="$cmds abs $x 705 wait 15"; done; done
echo "bar=$BP comp=$CP"
b0=$(cpu $BP); c0=$(cpu $CP); sleep $T; b1=$(cpu $BP); c1=$(cpu $CP)
echo "IDLE ${T}s bar_ms=$((b1-b0)) comp_ms=$((c1-c0))"
b0=$(cpu $BP); c0=$(cpu $CP)
timeout $T bash -c "while :; do $d/ptr $cmds >/dev/null 2>&1; done"
b1=$(cpu $BP); c1=$(cpu $CP)
echo "SWEEP ${T}s bar_ms=$((b1-b0)) comp_ms=$((c1-c0))"
kill $CP
