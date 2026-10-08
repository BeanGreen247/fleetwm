#!/bin/bash
# flagab-safe.sh REPS SECS V... : like flagab.sh but kills only recorded PIDs (safe next to a live session).
# per variant: nested headless compositor, 20 s of foot running yes, compositor CPU ms (schedstat) and foot CPU ticks; also idle CPU over the same time.
REPS=$1; T=$2; shift 2
P=/home/dev/perf-test
cpu(){ awk '{print int($1/1000000)}' /proc/$1/schedstat; }
for r in $(seq $REPS); do for V in "$@"; do
  bash $P/nest_headless.sh $V >/dev/null 2>&1; sleep 3
  RT=/tmp/ptest-run-$V; CP=$(cat $RT/pid)
  XDG_RUNTIME_DIR=$RT WAYLAND_DISPLAY=wayland-0 foot -F -e yes >/dev/null 2>&1 & FP=$!
  sleep 5
  c0=$(cpu $CP); f0=$(awk '{print $14+$15}' /proc/$FP/stat); sleep $T; c1=$(cpu $CP); f1=$(awk '{print $14+$15}' /proc/$FP/stat)
  kill $FP; wait $FP 2>/dev/null; sleep 3
  i0=$(cpu $CP); sleep 20; i1=$(cpu $CP)
  echo "$V scroll comp_ms=$((c1-c0)) foot_ticks=$((f1-f0)) idle20s_comp_ms=$((i1-i0))"
  kill $CP; sleep 1
done; done
