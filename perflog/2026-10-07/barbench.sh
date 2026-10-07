#!/bin/bash
# usage: barbench.sh SECONDS ROUNDS VARIANT...  -- bar CPU (task-clock ms) in a nested compositor with a charging fake battery
T=$1; R=$2; shift 2
mkdir -p /tmp/fakebat; echo 40 > /tmp/fakebat/capacity; echo Charging > /tmp/fakebat/status
for r in $(seq 1 $R); do
  for V in "$@"; do
    FLEETWM_BATTERY_DIR=/tmp/fakebat /home/dev/perf-test/nest.sh $V >/dev/null
    CP=$(cat /tmp/ptest-run-$V/pid); sleep 4
    BP=$(pgrep -P $CP -x fleetwm-bar | head -1)
    ms=$(perf stat -x, -e task-clock -p $BP -- sleep $T 2>&1 | awk -F, '{printf "%.0f", $1}')
    echo "round $r $V bar_pid=$BP task-clock-ms=$ms (gov $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor))"
    kill $CP; sleep 2
  done
done
