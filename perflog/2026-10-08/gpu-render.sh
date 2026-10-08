#!/bin/bash
# gpu-render.sh REPS SECS VARIANT : nested headless compositor once with pixman and once with gles2 (real GPU via the render node), interleaved.
# Per run: 20 s of foot running yes, compositor CPU ms (schedstat), foot CPU ticks, GPU busy % = 100 - rc6 residency share over the window (i915), mean act freq.
REPS=$1; T=$2; V=${3:-gen}
P=/home/dev/perf-test
RC6=$(ls /sys/class/drm/card*/gt/gt0/rc6_residency_ms | head -1); FRQ=$(dirname $RC6)/rps_act_freq_mhz
cpu(){ awk '{print int($1/1000000)}' /proc/$1/schedstat; }
for r in $(seq $REPS); do for R in pixman gles2; do
  RT=/tmp/ptest-run-$V
  WLR_RENDERER=$R bash $P/nest_headless.sh $V >/dev/null 2>&1; sleep 3
  CP=$(cat $RT/pid)
  grep -m1 -i 'renderer' $RT/compositor.log | cut -c1-90 > /tmp/gpu-rl.txt
  XDG_RUNTIME_DIR=$RT WAYLAND_DISPLAY=wayland-0 foot -F -e yes >/dev/null 2>&1 & FP=$!
  sleep 5
  c0=$(cpu $CP); f0=$(awk '{print $14+$15}' /proc/$FP/stat); g0=$(cat $RC6); t0=$(date +%s%N); fs=0
  for i in 1 2 3 4; do sleep $((T/4)); fs=$((fs+$(cat $FRQ))); done
  c1=$(cpu $CP); f1=$(awk '{print $14+$15}' /proc/$FP/stat); g1=$(cat $RC6); t1=$(date +%s%N)
  kill $FP; wait $FP 2>/dev/null
  ms=$(( (t1-t0)/1000000 )); busy=$(( 100 - 100*(g1-g0)/ms ))
  echo "$R comp_ms=$((c1-c0)) foot_ticks=$((f1-f0)) gpu_busy_pct=$busy gpu_mhz=$((fs/4)) $(cat /tmp/gpu-rl.txt)"
  kill $CP; sleep 1
done; done
