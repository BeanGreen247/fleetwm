#!/bin/bash
# usage: ab.sh LABEL SRC_DIR [seconds]  -- one headless training session of SRC_DIR/build-pgo, CPU from the cgroup, PSS sampled at 60% of the run
label=$1; src=$2; secs=${3:-85}
cg=/sys/fs/cgroup$(sed 's/^0:://' /proc/self/cgroup)
rt=$(mktemp -d /tmp/fleetwm-ab.XXXXXX)
( sleep $((secs*6/10)); tot=0; for p in $(cat $cg/cgroup.procs); do v=$(awk '/^Pss:/{print $2}' /proc/$p/smaps_rollup 2>/dev/null); tot=$((tot+${v:-0})); done; echo $tot > $rt/pss ) &
dbus-run-session -- env WLR_BACKENDS=headless WLR_RENDERER=pixman XDG_RUNTIME_DIR=$rt HOME=$rt \
  bash $src/scripts/pgo-train-session.sh $rt $secs $src/build-pgo > $rt/train.log 2>&1
wait
u=$(awk '/^usage_usec/{print $2}' $cg/cpu.stat)
echo "$label cpu_s=$(awk -v u=$u 'BEGIN{printf "%.1f",u/1e6}') pss_mb=$(awk -v k=$(cat $rt/pss 2>/dev/null || echo 0) 'BEGIN{printf "%.1f",k/1024}')"
