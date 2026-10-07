#!/bin/bash
# usage: glassbench.sh VARIANT ROUNDS   -- compositor+bar CPU (task-clock ms) for the same pointer workload with glass on and off,
# interleaved, in a throwaway nested compositor on the laptop. Workload: open Settings, drag its titlebar and its right edge 12 times each way.
V=$1; R=${2:-3}
PT=/tmp/gb/pgo-pointer
for r in $(seq 1 $R); do
  for GL in true false; do
    /home/dev/perf-test/nest.sh $V >/dev/null
    HM=/tmp/ptest-home-$V; RT=/tmp/ptest-run-$V; CP=$(cat $RT/pid)
    sed -i "s/^glass_effects.*/glass_effects = $GL/" $HM/.config/fleetwm/theme.toml; grep -q glass_effects $HM/.config/fleetwm/theme.toml || echo "glass_effects = $GL" >> $HM/.config/fleetwm/theme.toml
    sleep 1.5
    export XDG_RUNTIME_DIR=$RT WAYLAND_DISPLAY=wayland-0
    /home/dev/perf-test/E/apps/settings/fleetwm-settings --page theme >/dev/null 2>&1 & SP=$!
    sleep 3
    BP=$(pgrep -P $CP -x fleetwm-bar | head -1)
    # warm up one pass so caches are filled, then measure
    $PT drag 645 70 445 250 6 drag 445 250 645 70 6 >/dev/null 2>&1
    t0=$(date +%s.%N)
    perf stat -x, -e task-clock -p $CP,$BP -- bash -c "for i in \$(seq 1 12); do $PT drag 645 70 445 250 8 drag 445 250 645 70 8 drag 1049 300 1149 300 8 drag 1149 300 1049 300 8; done" 2>&1 | awk -F, '{printf "%.0f", $1}' > /tmp/gb_ms.txt
    t1=$(date +%s.%N)
    echo "round $r glass=$GL cpu_ms=$(cat /tmp/gb_ms.txt) wall_s=$(echo "$t1 - $t0" | bc -l | cut -c1-5) gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)"
    kill $SP $CP 2>/dev/null; sleep 2
  done
done
