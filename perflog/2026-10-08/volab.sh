#!/bin/bash
# usage: volab.sh VARIANT SECONDS REPS -> bar CPU incl. children (utime+stime+cutime+cstime, ms) and wake-ups/s,
# interleaving: native PipeWire (PIPEWIRE_RUNTIME_DIR points at the VM's real server) vs wpctl fallback (no server in the nested runtime dir)
V=${1:-gen}; T=${2:-60}; R=${3:-4}
B=/home/dev/perf-test/$V/bin; RT=/tmp/ptest-run-$V; HM=/tmp/ptest-home-$V
bash /home/dev/perf-test/nest_headless.sh $V >/dev/null
sed -i "s/^show_seconds = .*/show_seconds = false/" $HM/.config/fleetwm/bar.toml
export XDG_RUNTIME_DIR=$RT HOME=$HM PATH=$B:$PATH
export WAYLAND_DISPLAY=$(basename $(ls $RT/wayland-* | grep -v lock | head -1))
tck=$(getconf CLK_TCK)
run() { # mode
  if [ $1 = native ]; then export PIPEWIRE_RUNTIME_DIR=/run/user/1000; else unset PIPEWIRE_RUNTIME_DIR; fi
  setsid nohup $B/fleetwm-bar >$RT/bar.log 2>&1 </dev/null & 
  sleep 4; P=$(pgrep -x -u dev fleetwm-bar | head -1)
  a=($(awk '{print $14+$15+$16+$17}' /proc/$P/stat)); sa=$(awk '/ctxt/{s+=$2}END{print s}' /proc/$P/task/*/status)
  sleep $T
  b=$(awk '{print $14+$15+$16+$17}' /proc/$P/stat); sb=$(awk '/ctxt/{s+=$2}END{print s}' /proc/$P/task/*/status)
  echo "$1 cpu_ms=$(( (b-a)*1000/tck )) wake/s=$(awk -v a=$sa -v b=$sb -v t=$T 'BEGIN{printf "%.2f",(b-a)/t}') threads=$(ls /proc/$P/task | wc -l)"
  kill $P; sleep 1
}
for i in $(seq $R); do run fallback; run native; done
kill $(cat $RT/pid)
