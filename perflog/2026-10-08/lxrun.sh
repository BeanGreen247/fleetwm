#!/bin/bash
# lxrun.sh REPS : Lestrix (SDL wayland client) in nested headless Fleetwm: idle 15 s, then flood `seq 1 4000000`, normal vs --lite
REPS=$1; P=/home/dev/perf-test; LX=$HOME/lestrix/native/build/lestrix
cpu(){ awk '{print int($1/1000000)}' /proc/$1/schedstat; }
for r in $(seq $REPS); do for mode in normal lite; do
  pkill -u dev -x fleetwm; pkill -u dev -x lestrix; sleep 1
  bash $P/nest_headless.sh gen >/dev/null 2>&1; sleep 3
  RT=/tmp/ptest-run-gen; export XDG_RUNTIME_DIR=$RT WAYLAND_DISPLAY=wayland-0 SDL_VIDEODRIVER=wayland
  CP=$(cat $RT/pid); D=/tmp/lxwork; rm -rf $D; mkdir $D
  opts=""; [ $mode = lite ] && opts="--lite"
  $LX --working-directory $D $opts -e sh -c "sleep 17; seq 1 4000000; touch d; sleep 3" >/tmp/lx.out 2>&1 & LP=$!
  sleep 4
  c0=$(cpu $CP); l0=$(cpu $LP); sleep 12; c1=$(cpu $CP); l1=$(cpu $LP)
  echo "$mode idle12s comp_ms=$((c1-c0)) lestrix_ms=$((l1-l0))"
  # flood starts after the 15 s wait; time until d appears
  c0=$(cpu $CP); l0=$(cpu $LP); s=$(date +%s.%N)
  for i in $(seq 1 300); do [ -e $D/d ] && break; sleep 0.2; done
  e=$(date +%s.%N); c1=$(cpu $CP); l1=$(cpu $LP)
  echo "$mode flood+wait $(awk -v s=$s -v e=$e 'BEGIN{printf "%.1f",e-s}')s comp_ms=$((c1-c0)) lestrix_ms=$((l1-l0)) done=$([ -e $D/d ] && echo yes || echo NO)"
  kill $LP 2>/dev/null; wait $LP 2>/dev/null; kill $CP; sleep 1
done; done
