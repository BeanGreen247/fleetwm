#!/usr/bin/env bash
# Synthetic training pass for scripts/build-pgo-auto.sh -- exercises the
# instrumented (profile-generate) binaries so their .gcda files reflect
# real code paths instead of only "process started and immediately
# quit." Meant to run inside `dbus-run-session` (an isolated session
# D-Bus bus is required: without one, every GTK4 client's GApplication
# silently hands off to whatever real fleetwm-bar/-settings/etc is
# already running on the actual desktop session's bus instead of doing
# any work itself -- confirmed the hard way while building this script).
#
# Not set -e: one client failing to start shouldn't abort the whole
# training pass and skip cleanup of everything already running.
set -uo pipefail

RUNTIME_DIR="$1"
TRAIN_SECONDS="$2"
BUILD_DIR="$3"

IPC_SOCK="${RUNTIME_DIR}/fleetwm.sock"
COMP_LOG="${RUNTIME_DIR}/compositor.log"

CHILD_PIDS=()

cleanup() {
  # Reverse order: clients before the compositor they depend on. Every
  # binary here calls fleetwm::install_clean_quit() (src/common/
  # clean_quit.cpp), so SIGTERM reaches g_application_quit()/
  # wl_display_terminate() -- not the kernel's raw "terminate
  # immediately" default -- which is what lets gcov's atexit-based
  # flush actually run and write real .gcda data.
  for ((i = ${#CHILD_PIDS[@]} - 1; i >= 0; i--)); do
    pid="${CHILD_PIDS[$i]}"
    kill -TERM "$pid" 2>/dev/null || continue
    wait "$pid" 2>/dev/null || true
  done
}
trap cleanup EXIT

echo "    starting instrumented compositor (headless backend)..."
"${BUILD_DIR}/src/compositor/fleetwm" >"$COMP_LOG" 2>&1 &
comp_pid=$!
CHILD_PIDS+=("$comp_pid")

for _ in $(seq 1 50); do
  [[ -S "$IPC_SOCK" ]] && break
  if ! kill -0 "$comp_pid" 2>/dev/null; then
    echo "    compositor exited before opening its IPC socket:" >&2
    cat "$COMP_LOG" >&2
    exit 1
  fi
  sleep 0.2
done
if [[ ! -S "$IPC_SOCK" ]]; then
  echo "    compositor never opened its IPC socket, giving up" >&2
  exit 1
fi
export WAYLAND_DISPLAY=wayland-0

send_ipc() {
  python3 - "$IPC_SOCK" "$1" <<'PY'
import socket, sys
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.settimeout(2)
sock.connect(sys.argv[1])
sock.sendall((sys.argv[2] + "\n").encode())
sock.close()
PY
}

spawn_client() {
  # $1 = binary path, $2 = seconds to let it run before the final
  # cleanup pass quits it (each client keeps running independently in
  # the background -- this just staggers their startup slightly so
  # they're not all hitting the config/theme files at the exact same
  # instant).
  "$1" >"${RUNTIME_DIR}/$(basename "$1").log" 2>&1 &
  CHILD_PIDS+=("$!")
}

echo "    exercising compositor IPC (workspace switches)..."
send_ipc "WORKSPACE 1"
send_ipc "WORKSPACE?"
send_ipc "WORKSPACE 2"
send_ipc "WORKSPACE 0"

echo "    starting GTK4 clients: bar, wallpaper, settings, launcher, powermenu, audiomixer..."
# Locker and the greeter binaries are deliberately not included here --
# locker needs a real PAM auth round trip to reach its own clean-unlock
# exit path, and the greeter is TTY/PAM-driven, not a GTK4
# GApplication; both stay accepted gaps in headless training, same as
# they were in the last manual live-usage training run (see
# project_fleetwm_tests.md / project_fleetwm_backlog.md).
spawn_client "${BUILD_DIR}/src/bar/fleetwm-bar"
spawn_client "${BUILD_DIR}/src/wallpaper/fleetwm-wallpaper"
spawn_client "${BUILD_DIR}/src/settings/fleetwm-settings"
spawn_client "${BUILD_DIR}/src/launcher/fleetwm-launcher"
spawn_client "${BUILD_DIR}/src/powermenu/fleetwm-powermenu"
spawn_client "${BUILD_DIR}/src/audiomixer/fleetwm-audiomixer"

echo "    letting everything run for ${TRAIN_SECONDS}s..."
sleep "$TRAIN_SECONDS"

echo "    more workspace switches..."
send_ipc "WORKSPACE 1"
send_ipc "WORKSPACE 0"

echo "    training pass complete, shutting everything down cleanly..."
# cleanup() (the EXIT trap) does the actual SIGTERM + wait for every
# child, compositor last.
