#!/bin/bash
# hover2.sh SECS REPS : nested headless compositor (gen build), the bar started by hand. Interleaves three 
# conditions per rep: idle, pointer sweep across the bar (about 130 motion events/s, the old hover.sh pattern, now with a
# pointer that really reaches the bar) and the pointer resting on the CPU widget (its tooltip refreshes every second).
# Prints bar and compositor CPU ms (schedstat) per window. Needs /tmp/ptr/ptr built from scripts/pgo-pointer.c.
T=${1:-20}; R=${2:-3}; V=gen; B=/home/dev/perf-test/$V/bin
bash /home/dev/perf-test/nest_headless.sh $V >/dev/null
RT=/tmp/ptest-run-$V; HM=/tmp/ptest-home-$V
sed -i "s/^show_seconds = .*/show_seconds = false/" $HM/.config/fleetwm/bar.toml
export XDG_RUNTIME_DIR=$RT HOME=$HM PATH=$B:$PATH
export WAYLAND_DISPLAY=$(basename $(ls $RT/wayland-* | grep -v lock | head -1))
setsid nohup $B/fleetwm-bar >$RT/bar.log 2>&1 </dev/null &
sleep 4
CP=$(cat $RT/pid); BP=$(pgrep -x -u dev fleetwm-bar | head -1)
cpu(){ awk '{print int($1/1000000)}' /proc/$1/schedstat; }
cmds=""; for r in 1 2 3 4 5 6 7 8 9 10 11 12; do for x in $(seq 20 40 1260); do cmds="$cmds abs $x 697 wait 15"; done; done
win(){ # name, command...
  n=$1; shift; b0=$(cpu $BP); c0=$(cpu $CP)
  if [ $# -gt 0 ]; then timeout $T "$@" >/dev/null 2>&1; else sleep $T; fi
  echo "$n bar_ms=$(( $(cpu $BP)-b0 )) comp_ms=$(( $(cpu $CP)-c0 ))"
}
for i in $(seq $R); do
  win idle
  win sweep bash -c "while :; do /tmp/ptr/ptr $cmds; done"
  win rest_cpu /tmp/ptr/ptr abs 860 690 wait $((T*1000))
  win rest_blank /tmp/ptr/ptr abs 400 300 wait $((T*1000))
done
pkill -u dev -x fleetwm-bar; kill $CP
