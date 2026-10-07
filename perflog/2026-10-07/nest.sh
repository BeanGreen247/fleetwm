#!/bin/bash
# usage: nest.sh VARIANT   -> starts a throwaway nested compositor, prints "PID=<pid> DISPLAY=<name> RT=<dir>"
V=$1
RT=/tmp/ptest-run-$V; HM=/tmp/ptest-home-$V
rm -rf $RT $HM; mkdir -m 700 $RT; mkdir -p $HM/.config; cp -r /home/dev/.config/fleetwm $HM/.config/
export XDG_RUNTIME_DIR=$RT HOME=$HM
export WAYLAND_DISPLAY=/run/user/1000/wayland-0 WLR_BACKENDS=wayland
export PATH=/home/dev/perf-test/$V/bin:$PATH
unset FLEETWM_SESSION
setsid nohup /home/dev/perf-test/$V/bin/fleetwm > $RT/compositor.log 2>&1 < /dev/null &
echo $! > $RT/pid
for i in $(seq 1 50); do ls $RT/wayland-* >/dev/null 2>&1 && break; sleep 0.2; done
sleep 2
echo "PID=$(cat $RT/pid) RT=$RT HOME=$HM"; ls $RT
