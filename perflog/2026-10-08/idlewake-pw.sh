#!/bin/bash
# usage: idlewake.sh VARIANT SECONDS   (run on the dev VM) -> wake-ups/s and CPU ms per process, all idle
# Nested headless compositor + bar + wallpaper + launcher + lockapplet; counts voluntary+involuntary
# context switches (all threads) and schedstat CPU over a fixed window. Never touches the live session.
V=${1:-gen}; T=${2:-60}
B=/home/dev/perf-test/$V/bin
bash /home/dev/perf-test/nest_headless.sh $V >/dev/null
RT=/tmp/ptest-run-$V; HM=/tmp/ptest-home-$V
export XDG_RUNTIME_DIR=$RT HOME=$HM WLR_BACKENDS=headless WLR_RENDERER=pixman PATH=$B:$PATH
export WAYLAND_DISPLAY=$(basename $(ls $RT/wayland-* | grep -v lock | head -1))
CP=$(cat $RT/pid); ln -sf /run/user/1000/pipewire-0 $RT/pipewire-0
sed -i "s/^show_seconds = .*/show_seconds = ${SECS:-false}/" $HM/.config/fleetwm/bar.toml
for p in fleetwm-bar fleetwm-wallpaper fleetwm-launcher fleetwm-lockapplet; do
  setsid nohup $B/$p >$RT/$p.log 2>&1 </dev/null & echo $! >$RT/$p.pid
done
sleep 5
snap() { # pid -> "switches cpu_ns threads"
  local p=$1 sw=0 cpu=0 n=0
  for t in /proc/$p/task/*; do
    s=$(awk '/ctxt_switches/{a+=$2}END{print a+0}' $t/status 2>/dev/null)
    c=$(awk '{print $1}' $t/schedstat 2>/dev/null)
    sw=$((sw+${s:-0})); cpu=$((cpu+${c:-0})); n=$((n+1))
  done
  echo "$sw $cpu $n"
}
declare -A A
names="fleetwm:$CP"
for p in fleetwm-bar fleetwm-wallpaper fleetwm-launcher fleetwm-lockapplet; do names="$names $p:$(cat $RT/$p.pid)"; done
for e in $names; do A[$e]=$(snap ${e#*:}); done
sleep $T
for e in $names; do
  b=$(snap ${e#*:}); set -- ${A[$e]} $b
  printf "%-22s threads=%s  wake/s=%s  cpu_ms=%s\n" ${e%%:*} $3 $(awk -v a=$1 -v b=$4 -v t=$T 'BEGIN{printf "%.2f",(b-a)/t}') $(awk -v a=$2 -v b=$5 'BEGIN{printf "%.2f",(b-a)/1e6}')
done
for p in fleetwm-bar fleetwm-wallpaper fleetwm-launcher fleetwm-lockapplet; do echo "$p alive=$(kill -0 $(cat $RT/$p.pid) 2>/dev/null && echo y || echo n) log: $(head -c 150 $RT/$p.log | tr "\n" " ")"; kill $(cat $RT/$p.pid) 2>/dev/null; done
kill $CP 2>/dev/null
