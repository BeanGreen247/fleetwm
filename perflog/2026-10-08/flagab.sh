#!/bin/bash
# flagab.sh REPS SECS V... : interleaved; per variant start a nested headless compositor, 20 s of foot running yes, report compositor CPU ms
REPS=$1; T=$2; shift 2
P=/home/dev/perf-test
for V in "$@"; do mkdir -p $P/$V/bin; for f in $(find $P/b$V -name "fleetwm*" -type f -executable); do ln -sf $f $P/$V/bin/$(basename $f); done; done
cpu(){ awk '{print int($1/1000000)}' /proc/$1/schedstat; }
for r in $(seq $REPS); do for V in "$@"; do
  pkill -u dev -x fleetwm; sleep 1
  bash $P/nest_headless.sh $V >/dev/null 2>&1; sleep 3
  RT=/tmp/ptest-run-$V; export XDG_RUNTIME_DIR=$RT WAYLAND_DISPLAY=wayland-0
  CP=$(cat $RT/pid)
  foot -F -e yes >/dev/null 2>&1 & FP=$!
  sleep 5
  c0=$(cpu $CP); f0=$(awk '{print $14+$15}' /proc/$FP/stat); sleep $T; c1=$(cpu $CP); f1=$(awk '{print $14+$15}' /proc/$FP/stat)
  kill $FP; wait $FP 2>/dev/null
  echo "$V scroll comp_ms=$((c1-c0)) foot_ticks=$((f1-f0))"
  kill $CP; sleep 1
done; done
