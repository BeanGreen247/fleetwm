#!/bin/bash
# usage: scrollbench.sh VARIANT SECONDS   (nested compositor VARIANT must be running, see nest.sh)
# full-window foot running `yes`; reports task-clock (CPU ms) of the nested compositor and the bar over the window
V=$1; T=${2:-20}
RT=/tmp/ptest-run-$V; CP=$(cat $RT/pid)
export XDG_RUNTIME_DIR=$RT WAYLAND_DISPLAY=wayland-0
foot -F -c /usr/local/etc/fleetwm/foot.ini -e yes >/dev/null 2>&1 &
FP=$!
sleep 4
perf stat -x, -e task-clock -p $CP -- sleep $T 2>&1 | awk -F, -v v=$V '{printf "compositor %s task-clock-ms %.0f\n", v, $1}'
cat /proc/$FP/stat | awk '{print "foot utime+stime ticks (total so far)", $14+$15}'
kill $FP; wait $FP 2>/dev/null
