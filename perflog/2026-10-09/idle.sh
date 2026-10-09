#!/bin/bash
# usage: idle.sh BARBIN SECONDS  -> prints "wakeups/s cpu_ms/s rss_kb" of an idle bar after 8 s settling
BAR=$1; SECS=${2:-60}
export XDG_RUNTIME_DIR=/tmp/rt HOME=/tmp/h WLR_BACKENDS=headless WLR_RENDERER=pixman LANG=C.UTF-8 LC_ALL=C.UTF-8
rm -rf /tmp/rt /tmp/h; mkdir -p /tmp/rt /tmp/h/.config/fleetwm; chmod 700 /tmp/rt
printf 'window_layout = "desktop"\nglass_effects = true\n' > /tmp/h/.config/fleetwm/theme.toml
cat > /tmp/idle_inner.sh <<'IN'
/w/build-dev/src/compositor/fleetwm > /tmp/rt/comp.log 2>&1 &
CP=$!
for i in $(seq 1 100); do [ -S /tmp/rt/fleetwm.sock ] && break; sleep 0.1; done
export WAYLAND_DISPLAY=wayland-0
sleep 2
$BAR > /tmp/rt/bar.log 2>&1 &
BP=$!
sleep 8
sw() { cat /proc/$BP/task/*/status | awk '/ctxt_switches/ {s+=$2} END {print s}'; }
cpu() { awk '{print $14+$15}' /proc/$BP/stat; }
a=$(sw); c=$(cpu)
sleep $SECS
b=$(sw); d=$(cpu)
rss=$(awk '/VmRSS/ {print $2}' /proc/$BP/status)
tck=$(getconf CLK_TCK)
echo "$(( (b-a) * 100 / SECS ))e-2 wakeups/s  $(( (d-c) * 1000 / tck / SECS )) ms-cpu/s  rss ${rss} kB"
kill -TERM $BP $CP 2>/dev/null; sleep 0.5
IN
BAR=$BAR SECS=$SECS dbus-run-session -- bash /tmp/idle_inner.sh 2>&1 | tail -n 1
