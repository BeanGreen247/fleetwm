#!/usr/bin/env bash
# Window move / resize cost, measured from inside the compositor (FLEETWM_GRAB_STATS, src/compositor/grab_stats.hpp) and, with --gpu, from
# intel_gpu_top attributed to the compositor's pid. Runs a headless compositor of its own (own runtime directory and HOME): the live session,
# the screen and the user's settings are never touched. The only kill is by the pids this script started.
#
#   scripts/grab-stats-run.sh BUILD_DIR OUT_DIR [--renderer gles2|pixman] [--size 1920x1080] [--glass true|false] [--rounds 3] [--gpu] [--win-scale N] [--terminals N]
#   --terminals N: N `foot` terminals that keep printing (window sizes FW x FH, default 640x340) instead of the Settings window; the last one is the front
#   window and is the one dragged, so the glass of its frame sits over N-1 windows that repaint (needs foot). The titlebar is aimed from the cascade rule
#   (no screenshot tool needed); TB (taskbar height, default 40) and TITLE_DY adjust it.
#   (environment WINDOW_SHADOWS=true|false sets theme.toml window_shadows, default true; COMP_WRAP="valgrind --tool=callgrind ..." starts the compositor under
#   that command, then also set COMP_GRACE=60 (wait for the tool to write its file when the run ends) and SOCK_WAIT=1200 (tenths of a second to wait for it) and ONLYIDLE=SECONDS or ONLYMOVE=SECONDS to run just one idle or window-move phase)
#
# BUILD_DIR holds src/compositor/fleetwm, src/bar/fleetwm-bar, src/wallpaper/fleetwm-wallpaper and apps/settings/fleetwm-settings.
# Phases per round: idle 8 s, move 8 s (titlebar drags), resize 8 s (right-edge drags), idle 4 s. Results: OUT_DIR/summary.txt (median of each
# GRABSTATS field per mode) and, with --gpu, the Render/3D busy of the compositor per phase. --gpu needs `sudo intel_gpu_top`: it uses
# `sudo -n`, or `sudo -S` with the password in $GPU_SUDO_PW (never printed or written down).
set -uo pipefail
BUILD=$(realpath "$1"); OUT=$(mkdir -p "$2" && realpath "$2"); shift 2
RENDERER=gles2; SIZE=1920x1080; GLASS=true; ROUNDS=3; GPU=0; NFOOT=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --renderer) RENDERER=$2; shift 2 ;;
    --size) SIZE=$2; shift 2 ;;
    --glass) GLASS=$2; shift 2 ;;
    --rounds) ROUNDS=$2; shift 2 ;;
    --gpu) GPU=1; shift ;;
    --terminals) NFOOT=$2; shift 2 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done
W=${SIZE%x*}; H=${SIZE#*x}
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
RT=$(mktemp -d /tmp/fleetwm-gs.XXXXXX); chmod 700 "$RT"
HM=$RT/home; mkdir -p "$HM/.config/fleetwm"
printf 'window_layout = "desktop"\nglass_effects = %s\nwindow_shadows = %s\n' "$GLASS" "${WINDOW_SHADOWS:-true}" > "$HM/.config/fleetwm/theme.toml"
PIDS=()
cleanup() {
  local p
  # the compositor is a grandchild (dbus-run-session -> bash -> fleetwm): killing the wrapper alone left it, and its lock applet, running forever
  # on the machine (43 of them piled up on the laptop); its pid is in $RT/comp.pid
  if [[ -f $RT/comp.pid ]]; then
    local cp; cp=$(cat "$RT/comp.pid")
    pkill -TERM -P "$cp" 2>/dev/null  # its autostarted programs (lock applet) by parent pid, not by name
    if [[ -n ${COMP_GRACE:-} ]]; then
      # COMP_GRACE=SECONDS: a wrapped compositor (heaptrack, massif, callgrind) gets only the compositor itself terminated, then time to write its
      # result file; the wrapper is never signalled, so its output stage is not cut short
      for ((t = 0; t < COMP_GRACE * 10; t++)); do kill -0 "$cp" 2>/dev/null || break; sleep 0.1; done
    fi
    kill -TERM "$cp" 2>/dev/null
  fi
  for ((i=${#PIDS[@]}-1; i>=0; i--)); do p=${PIDS[$i]}; kill -TERM "$p" 2>/dev/null; done
  sleep 0.5
  rm -rf "$RT"
}
trap cleanup EXIT

# the virtual pointer helper (drags through wlr-virtual-pointer; built here from the repo's source)
PTR=$OUT/pgo-pointer
if [[ ! -x $PTR ]]; then
  d=$RT/ptr; mkdir -p "$d"
  wayland-scanner client-header "$ROOT/scripts/wlr-virtual-pointer-unstable-v1.xml" "$d/wlr-virtual-pointer-unstable-v1-client-protocol.h" &&
    wayland-scanner private-code "$ROOT/scripts/wlr-virtual-pointer-unstable-v1.xml" "$d/code.c" &&
    cc -O1 -w -I"$d" -o "$PTR" "$ROOT/scripts/pgo-pointer.c" "$d/code.c" -lwayland-client || { echo "could not build the pointer helper" >&2; exit 1; }
fi

export XDG_RUNTIME_DIR=$RT HOME=$HM WLR_BACKENDS=headless WLR_RENDERER=$RENDERER FLEETWM_GRAB_STATS=${GRAB_STATS_LEVEL:-1} LANG=C.UTF-8 LC_ALL=C.UTF-8
TAG="${RENDERER}-${SIZE}-glass${GLASS}-shadow${WINDOW_SHADOWS:-true}"
LOG=$OUT/comp-$TAG.log
dbus-run-session -- bash -c '
  ${COMP_WRAP:-} "$0/src/compositor/fleetwm" >> "$1" 2>&1 &
  echo $! > "$2/comp.pid"
  wait
' "$BUILD" "$LOG" "$RT" &
PIDS+=("$!")
for _ in $(seq 1 "${SOCK_WAIT:-100}"); do [[ -S $RT/fleetwm.sock ]] && break; sleep 0.1; done
[[ -S $RT/fleetwm.sock ]] || { echo "compositor did not start; see $LOG" >&2; exit 1; }
sleep 0.5; CP=$(cat "$RT/comp.pid")
export WAYLAND_DISPLAY=wayland-0
ipc() { python3 - "$RT/fleetwm.sock" "$1" <<'PY'
import socket, sys, time
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.settimeout(0.5); s.connect(sys.argv[1]); s.sendall((sys.argv[2] + "\n").encode()); time.sleep(0.15)
try: print(s.recv(65536).decode(errors="replace").strip()[:2000])
except Exception: pass
PY
}
OUTNAME=$(ipc "OUTPUTS?" | awk '/^OUTPUT /{print $2; exit}')
ipc "OUTPUT_SET $OUTNAME $W $H 60000 0 0" >/dev/null; sleep 1
"$BUILD/src/wallpaper/fleetwm-wallpaper" >/dev/null 2>&1 & PIDS+=("$!")
"$BUILD/src/bar/fleetwm-bar" >/dev/null 2>&1 & PIDS+=("$!")
sleep 2
if (( NFOOT > 0 )); then
  for i in $(seq 0 $((NFOOT-1))); do
    foot --window-size-pixels=${FW:-640}x${FH:-340} -e bash -c 'while :; do seq 1 60; sleep 0.08; done' >/dev/null 2>&1 & PIDS+=("$!")
    sleep 1
  done
  sleep 2
  export PGO_W=$W PGO_H=$H
  OW=$((${FW:-640}+14)); OH=$((${FH:-340}+14+30)); STEP=$(( ((NFOOT-1) % 8) * 32 ))
  WX=$(( (W - OW)/2 + STEP - 112 )); WY=$(( (H - ${TB:-40} - OH)/2 + STEP - 112 )); (( WX < 0 )) && WX=0; (( WY < 0 )) && WY=0
  CX=${TITLE_X:-$((WX + OW/2))}; TY=${TITLE_Y:-$((WY + ${TITLE_DY:-14}))}; REDGE_X=$((WX + OW + 2)); REDGE_Y=$((WY + OH/2)); CLOSE_X=
  echo "front terminal guessed at $WX,$WY size ${OW}x${OH}; titlebar drag at $CX,$TY" | tee -a "$LOG" >&2
else
"$BUILD/apps/settings/fleetwm-settings" --page theme >/dev/null 2>&1 & PIDS+=("$!")
sleep 3
command -v grim >/dev/null && grim "$OUT/layout-$TAG.png" 2>/dev/null
export PGO_W=$W PGO_H=$H

# Where the Settings window is: found from a screenshot (its red close button), because the window is centred in the usable area and that depends on the
# bar and the version of Settings. TITLE_X / TITLE_Y / REDGE_X / REDGE_Y override it.
detect() {
  grim -t ppm "$OUT/detect-$TAG.ppm" 2>/dev/null || return 1
  python3 - "$OUT/detect-$TAG.ppm" <<'PY'
import sys
d = open(sys.argv[1], "rb").read()
parts = d.split(b"\n", 3)
w, h = map(int, parts[1].split())
px = parts[3]
xs, ys = [], []
for y in range(0, int(h * 0.8)):
    row = px[y * w * 3:(y + 1) * w * 3]
    for x in range(0, w):
        r, g, b = row[x * 3], row[x * 3 + 1], row[x * 3 + 2]
        if r > 140 and r > g + 60 and r > b + 60:  # the close button, solid or glassy
            xs.append(x); ys.append(y)
if len(xs) > 40:
    xs.sort(); ys.sort()
    print(xs[len(xs) // 2], ys[len(ys) // 2])
PY
}
read -r CLOSE_X CLOSE_Y < <(detect) || true
if [[ -n ${CLOSE_X:-} ]]; then
  CX=${TITLE_X:-$((CLOSE_X - 400))}; TY=${TITLE_Y:-$((CLOSE_Y + 10))}
  REDGE_X=${REDGE_X:-$((CLOSE_X + 20))}; REDGE_Y=${REDGE_Y:-$((CLOSE_Y + 340))}
  echo "window found: close button at $CLOSE_X,$CLOSE_Y; titlebar drag at $CX,$TY; right edge at $REDGE_X,$REDGE_Y" | tee -a "$LOG" >&2
else
  CX=${TITLE_X:-$((W / 2))}; TY=${TITLE_Y:-14}; REDGE_X=${REDGE_X:-$((W / 2 + 405))}; REDGE_Y=${REDGE_Y:-$((H / 2))}
  echo "window not found in the screenshot; using guessed positions" >&2
fi
fi

# SHRINK_DX / SHRINK_DY (pixels): drag the window's bottom-right corner inward first, to measure a smaller window; the window is found again afterwards.
if [[ -n ${SHRINK_DX:-} && -n ${CLOSE_X:-} ]]; then
  # the right edge (the same spot the resize phase uses, which is known to grab), then the bottom edge
  "$PTR" drag $((CLOSE_X + 20)) $((CLOSE_Y + 340)) $((CLOSE_X + 20 - SHRINK_DX)) $((CLOSE_Y + 340)) 60 >/dev/null 2>&1
  sleep 1
  "$PTR" drag $((CLOSE_X - 200 - SHRINK_DX)) $((CLOSE_Y + 668)) $((CLOSE_X - 200 - SHRINK_DX)) $((CLOSE_Y + 668 - SHRINK_DY)) 60 >/dev/null 2>&1
  sleep 1.5
  read -r CLOSE_X CLOSE_Y < <(detect) || true
  CX=${TITLE_X:-$((CLOSE_X - 150))}; TY=${TITLE_Y:-$((CLOSE_Y + 10))}
  REDGE_X=$((CLOSE_X + 20)); REDGE_Y=$((CLOSE_Y + 100))
  echo "window after shrink (damage rect of the next commit tells its size)" >> "$LOG"
  echo "after shrink: close button at $CLOSE_X,$CLOSE_Y; titlebar drag at $CX,$TY" | tee -a "$LOG" >&2
  grim "$OUT/layout-$TAG-shrunk.png" 2>/dev/null
fi

gpu_capture() {  # gpu_capture NAME SECONDS  (background; JSON to OUT/gpu-NAME.json)
  [[ $GPU = 1 ]] || return 0
  if sudo -n true 2>/dev/null; then sudo -n timeout "$2" intel_gpu_top -J -s 500 > "$OUT/gpu-$TAG-$1.json" 2>/dev/null &
  else printf '%s\n' "${GPU_SUDO_PW:-}" | sudo -S -p "" timeout "$2" intel_gpu_top -J -s 500 > "$OUT/gpu-$TAG-$1.json" 2>/dev/null & fi
}
phase() {  # phase NAME SECONDS command...
  local name=$1 secs=$2; shift 2
  echo "PHASE $name $(date +%s.%N)" >> "$LOG"
  gpu_capture "$name" "$((secs + 1))"; sleep 0.3
  local end=$(( $(date +%s) + secs )) t0 t1 c0 c1
  t0=$(date +%s.%N); c0=$(awk '{print $14 + $15}' "/proc/$CP/stat" 2>/dev/null || echo 0)
  if [[ $# -gt 0 ]]; then while (( $(date +%s) < end )); do "$@"; done; else sleep "$secs"; fi
  t1=$(date +%s.%N); c1=$(awk '{print $14 + $15}' "/proc/$CP/stat" 2>/dev/null || echo 0)
  echo "COMPCPU $name ticks=$((c1 - c0)) dt=$(echo "$t1 - $t0" | bc -l)" >> "$LOG"  # 100 ticks = 1 s of one core
  sleep 1.2
}
move_once() { "$PTR" drag $CX $TY $((CX - 220)) $((TY + 220)) 100 drag $((CX - 220)) $((TY + 220)) $CX $TY 100 >/dev/null 2>&1; }
resize_once() { "$PTR" drag $REDGE_X $REDGE_Y $((REDGE_X + 100)) $REDGE_Y 100 drag $((REDGE_X + 100)) $REDGE_Y $REDGE_X $REDGE_Y 100 >/dev/null 2>&1; }

if [[ -n ${ONLYIDLE:-} ]]; then phase idle "$ONLYIDLE"; echo "COMPOSITOR_PID $CP" >> "$LOG"; exit 0; fi
if [[ -n ${ONLYMOVE:-} ]]; then phase move "$ONLYMOVE" move_once; echo "COMPOSITOR_PID $CP" >> "$LOG"; exit 0; fi
for r in $(seq 1 "$ROUNDS"); do
  phase idle 8
  phase move 8 move_once
  phase resize 8 resize_once
  phase idle2 4
done
echo "COMPOSITOR_PID $CP" >> "$LOG"

python3 "$ROOT/scripts/grab-stats-summary.py" "$LOG" "$OUT" "$TAG" "$CP" | tee "$OUT/summary-$TAG.txt"
