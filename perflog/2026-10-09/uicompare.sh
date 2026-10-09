#!/usr/bin/env bash
# Whole-program comparison on one folder: fleetwm-fm under Fleetwm's own compositor (headless, pixman) against Caja (the MATE file
# manager, GTK3 + GVfs) under Xvfb. Same procedure for both: start, wait for the window and the folder to settle, sample the
# resident memory (VmRSS, VmHWM) and the CPU time spent so far, wait again, sample the CPU time to see what an idle window costs.
#   uicompare.sh FOLDER [SETTLE_SECONDS]
# Needs: build-bench/apps/fleetfm/fleetwm-fm, build/src/compositor/fleetwm, caja, Xvfb, dbus-run-session.
set -u
DIR="${1:?folder}"
SETTLE="${2:-8}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
FM="${FM:-$ROOT/build-bench/apps/fleetfm/fleetwm-fm}"
COMP="${COMP:-$ROOT/build/src/compositor/fleetwm}"
HZ=$(getconf CLK_TCK)

sample() {  # pid label
  local pid="$1" label="$2"
  local rss hwm thr t0 t1
  rss=$(awk '/VmRSS/ {print $2}' /proc/"$pid"/status); hwm=$(awk '/VmHWM/ {print $2}' /proc/"$pid"/status); thr=$(awk '/Threads/ {print $2}' /proc/"$pid"/status)
  t0=$(awk '{print $14+$15}' /proc/"$pid"/stat)
  sleep 5
  t1=$(awk '{print $14+$15}' /proc/"$pid"/stat)
  printf '%-10s RSS %6d kB  peak %6d kB  threads %2d  CPU until settled %5.2f s  CPU over 5 idle seconds %5.2f s\n' "$label" "$rss" "$hwm" "$thr" "$(python3 -c "print($t0/$HZ)")" "$(python3 -c "print(($t1-$t0)/$HZ)")"
}

RT=$(mktemp -d /tmp/fmrt.XXXXXX); chmod 700 "$RT"
( dbus-run-session -- env WLR_BACKENDS=headless WLR_RENDERER=pixman XDG_RUNTIME_DIR="$RT" HOME="$RT" LANG=C.UTF-8 LC_ALL=C.UTF-8 "$COMP" >"$RT/comp.log" 2>&1 & )
for _ in $(seq 1 50); do [[ -S "$RT/wayland-0" ]] && break; sleep 0.2; done
( XDG_RUNTIME_DIR="$RT" WAYLAND_DISPLAY=wayland-0 XDG_CONFIG_HOME="$RT/cfg" "$FM" "$DIR" >"$RT/fm.log" 2>&1 & )
sleep "$SETTLE"
PID=$(pgrep -x fleetwm-fm | head -1)
[[ -n "$PID" ]] && sample "$PID" "fleetwm-fm" || echo "fleetwm-fm did not start"
pkill -x fleetwm-fm; pgrep -x fleetwm | xargs -r kill -TERM; sleep 1
rm -rf "$RT"

if command -v caja >/dev/null && command -v Xvfb >/dev/null; then
  CRT=$(mktemp -d /tmp/fmcaja.XXXXXX); chmod 700 "$CRT"
  ( env XDG_RUNTIME_DIR="$CRT" HOME="$CRT" xvfb-run -a dbus-run-session -- caja --no-desktop "$DIR" >"$CRT/caja.log" 2>&1 & )
  sleep "$((SETTLE + 4))"
  CPID=$(pgrep -x caja | head -1)
  [[ -n "$CPID" ]] && sample "$CPID" "caja" || echo "caja did not start"
  pkill -x caja; pkill -x Xvfb; sleep 1
  rm -rf "$CRT"
fi
