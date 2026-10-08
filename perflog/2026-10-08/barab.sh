#!/bin/bash
# usage: barab.sh REPS SECS VARIANT... ; variant = binary path[:ENV=1]. Interleaved; prints cpu ms and voluntary ctx switches per run.
REPS=$1; T=$2; shift 2
P=/home/dev/perf-test
pkill -u dev -x fleetwm-bar 2>/dev/null; pkill -u dev -x fleetwm 2>/dev/null; sleep 1
bash $P/nest_headless.sh P >/dev/null 2>&1; sleep 2
RT=/tmp/ptest-run-P; HM=/tmp/ptest-home-P
sed -i 's/show_seconds = true/show_seconds = false/' $HM/.config/fleetwm/bar.toml
pkill -u dev -x fleetwm-bar; sleep 1
export XDG_RUNTIME_DIR=$RT WAYLAND_DISPLAY=wayland-0 HOME=$HM
for r in $(seq $REPS); do for v in "$@"; do
  bin=${v%%:*}; env=""; [ "$v" != "$bin" ] && env=${v#*:}
  env $env $bin >/dev/null 2>&1 & BP=$!
  sleep 8
  read -r a1 b1 < <(awk '{print $1, $2}' /proc/$BP/schedstat); c1=$(awk '/^voluntary_ctxt_switches/{print $2}' /proc/$BP/status)
  sleep $T
  read -r a2 b2 < <(awk '{print $1, $2}' /proc/$BP/schedstat); c2=$(awk '/^voluntary_ctxt_switches/{print $2}' /proc/$BP/status)
  kill $BP; wait $BP 2>/dev/null
  echo "$(basename $bin)${env:+:$env} cpu_ms=$(( (a2-a1)/1000000 )) wakeups_per_s=$(echo "scale=3; ($c2-$c1)/$T" | bc)"
done; done
pkill -u dev -x fleetwm
