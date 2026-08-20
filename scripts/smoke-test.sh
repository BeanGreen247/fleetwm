#!/usr/bin/env bash
# Compositor integration/smoke tests -- the layer the unit-test suite
# (tests/, ~267 pure-logic gtest cases) deliberately excludes: real
# wlroots compositor lifecycle, IPC wire behavior, and process
# lifecycle/signal handling. Complements, not replaces, the unit suite --
# see tests/meson.build's own comment for that boundary.
#
# Reuses the same headless-backend + isolated-D-Bus-session + scratch
# XDG_RUNTIME_DIR pattern scripts/build-pgo-auto.sh's training pass
# already established (see pgo-train-session.sh) -- proven to work
# without real DRM/seat/GPU, so this runs anywhere the project builds,
# not just on real display hardware.
#
# Usage: scripts/smoke-test.sh [path-to-build-dir, default: build]
#
# Exits 0 if every check passes, non-zero (with a description of the
# first failure) otherwise. Deliberately not a gtest binary: these
# checks spawn a real process tree and talk over a real socket, so they
# belong in the "slower, separate stage" CI bucket, not the
# always-run-after-every-change unit suite.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$(cd "${1:-${SCRIPT_DIR}/build}" && pwd)"
COMPOSITOR_BIN="${BUILD_DIR}/src/compositor/fleetwm"

if [[ ! -x "$COMPOSITOR_BIN" ]]; then
  echo "error: $COMPOSITOR_BIN not found or not executable -- build first (ninja -C ${BUILD_DIR})" >&2
  exit 1
fi
for tool in dbus-run-session python3; do
  command -v "$tool" >/dev/null 2>&1 || { echo "error: $tool is required but not found on PATH" >&2; exit 1; }
done

PASS=0
FAIL=0
fail() {
  echo "  [FAIL] $1" >&2
  FAIL=$((FAIL + 1))
}
pass() {
  echo "  [ OK ] $1"
  PASS=$((PASS + 1))
}

RUNTIME_DIR="$(mktemp -d /tmp/fleetwm-smoke.XXXXXX)"
chmod 700 "$RUNTIME_DIR"
IPC_SOCK="${RUNTIME_DIR}/fleetwm.sock"
COMP_LOG="${RUNTIME_DIR}/compositor.log"
# $! after `dbus-run-session -- ... &` is the *wrapper's* PID, not the
# compositor's -- dbus-run-session forks the given command as its own
# child rather than exec-replacing itself, so a SIGTERM aimed at $! does
# not reliably reach the compositor's own SIGTERM handler (found the
# hard way: first version of this script got exit-by-signal 143 and a
# leftover socket file on what should have been a clean shutdown).
# WRAPPER_PID is kept only so cleanup() can reap it; COMP_PID (resolved
# via pgrep once the compositor is confirmed up) is the PID every
# liveness/signal check below actually targets.
WRAPPER_PID=""
COMP_PID=""

cleanup() {
  if [[ -n "$COMP_PID" ]] && kill -0 "$COMP_PID" 2>/dev/null; then
    kill -TERM "$COMP_PID" 2>/dev/null || true
  fi
  if [[ -n "$WRAPPER_PID" ]]; then
    wait "$WRAPPER_PID" 2>/dev/null || true
  fi
  rm -rf "$RUNTIME_DIR"
}
trap cleanup EXIT

start_compositor() {
  dbus-run-session -- env \
    WLR_BACKENDS=headless WLR_RENDERER=pixman \
    XDG_RUNTIME_DIR="$RUNTIME_DIR" HOME="$RUNTIME_DIR" \
    LANG=C.UTF-8 LC_ALL=C.UTF-8 \
    "$COMPOSITOR_BIN" >"$COMP_LOG" 2>&1 &
  WRAPPER_PID=$!
}

resolve_compositor_pid() {
  for _ in $(seq 1 50); do
    COMP_PID="$(pgrep -f -x "$COMPOSITOR_BIN" | head -n1)"
    [[ -n "$COMP_PID" ]] && return 0
    if ! kill -0 "$WRAPPER_PID" 2>/dev/null; then
      return 1
    fi
    sleep 0.2
  done
  return 1
}

wait_for_socket() {
  for _ in $(seq 1 50); do
    [[ -S "$IPC_SOCK" ]] && return 0
    if ! kill -0 "$WRAPPER_PID" 2>/dev/null; then
      return 1
    fi
    sleep 0.2
  done
  return 1
}

# One connection kept open for the whole run (matches how the bar
# actually uses IpcClient -- see src/common/ipc_client.hpp's own doc
# comment) rather than reconnecting per command, so WORKSPACE_CHANGED
# broadcasts and reply ordering behave like a real client would see them.
ipc_query() {
  python3 - "$IPC_SOCK" "$1" <<'PY'
import socket, sys
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.settimeout(2)
sock.connect(sys.argv[1])
sock.sendall((sys.argv[2] + "\n").encode())
try:
    data = sock.recv(256)
except socket.timeout:
    data = b""
sock.close()
print(data.decode(errors="replace").strip())
PY
}

echo "==> Compositor smoke/integration tests"

echo "-- CompositorStarts / IpcSocketCreated"
start_compositor
if wait_for_socket && resolve_compositor_pid; then
  pass "compositor started and opened its IPC socket"
else
  fail "compositor did not open $IPC_SOCK within 10s (or its PID could not be resolved)"
  cat "$COMP_LOG" >&2
  echo
  echo "==> $PASS passed, $FAIL failed"
  exit 1
fi

echo "-- DefaultWorkspaceIsZero"
reply="$(ipc_query 'WORKSPACE?')"
[[ "$reply" == "0" ]] && pass "WORKSPACE? replies 0 on a fresh session" \
  || fail "WORKSPACE? replied '$reply', expected '0'"

echo "-- WorkspaceCanBeSwitched"
ipc_query 'WORKSPACE 3' >/dev/null
reply="$(ipc_query 'WORKSPACE?')"
[[ "$reply" == "3" ]] && pass "WORKSPACE 3 then WORKSPACE? replies 3" \
  || fail "WORKSPACE? replied '$reply' after switching to 3, expected '3'"

echo "-- OutOfRangeWorkspaceIndexIsIgnored"
ipc_query 'WORKSPACE 99' >/dev/null
reply="$(ipc_query 'WORKSPACE?')"
[[ "$reply" == "3" ]] && pass "WORKSPACE 99 (out of range) left active workspace at 3" \
  || fail "WORKSPACE? replied '$reply' after an out-of-range switch, expected unchanged '3'"

echo "-- NegativeWorkspaceIndexIsIgnored"
ipc_query 'WORKSPACE -1' >/dev/null
reply="$(ipc_query 'WORKSPACE?')"
[[ "$reply" == "3" ]] && pass "WORKSPACE -1 left active workspace at 3" \
  || fail "WORKSPACE? replied '$reply' after a negative switch, expected unchanged '3'"

echo "-- MalformedWorkspaceCommandDoesNotCrash"
ipc_query 'WORKSPACE banana' >/dev/null
if kill -0 "$COMP_PID" 2>/dev/null; then
  reply="$(ipc_query 'WORKSPACE?')"
  [[ "$reply" == "3" ]] && pass "malformed 'WORKSPACE banana' left active workspace at 3, compositor alive" \
    || fail "WORKSPACE? replied '$reply' after malformed command, expected unchanged '3'"
else
  fail "compositor crashed on malformed 'WORKSPACE banana' command"
fi

echo "-- UnknownCommandDoesNotCrash"
ipc_query 'NOT_A_REAL_COMMAND' >/dev/null
if kill -0 "$COMP_PID" 2>/dev/null; then
  pass "unknown IPC command left the compositor running"
else
  fail "compositor crashed on an unrecognized IPC command"
fi

echo "-- IpcStillWorksAfterSeveralRoundTrips"
ipc_query 'WORKSPACE 0' >/dev/null
reply="$(ipc_query 'WORKSPACE?')"
[[ "$reply" == "0" ]] && pass "IPC still responsive after prior checks (WORKSPACE 0 took effect)" \
  || fail "WORKSPACE? replied '$reply' after WORKSPACE 0, expected '0'"

echo "-- CompositorShutsDownCleanlyOnSigterm"
kill -TERM "$COMP_PID"
shutdown_ok=0
for _ in $(seq 1 50); do
  if ! kill -0 "$COMP_PID" 2>/dev/null; then
    shutdown_ok=1
    break
  fi
  sleep 0.2
done
if [[ "$shutdown_ok" == "1" ]]; then
  # COMP_PID (resolved via pgrep) isn't a job of this shell, so it can't
  # be `wait`ed directly for its exit status -- dbus-run-session (the
  # WRAPPER_PID job) propagates the child's real exit code as its own,
  # so wait on that instead now that the compositor itself has exited.
  wait "$WRAPPER_PID" 2>/dev/null
  exit_code=$?
  COMP_PID=""
  WRAPPER_PID=""
  if [[ "$exit_code" == "0" ]]; then
    pass "compositor exited 0 within 10s of SIGTERM"
  else
    fail "compositor exited with code $exit_code after SIGTERM (expected 0)"
  fi
else
  fail "compositor did not exit within 10s of SIGTERM"
  cat "$COMP_LOG" >&2
fi

echo "-- IpcSocketRemovedAfterCleanShutdown"
[[ ! -S "$IPC_SOCK" ]] && pass "IPC socket file removed after clean shutdown" \
  || fail "IPC socket file still present after clean shutdown"

echo
echo "==> $PASS passed, $FAIL failed"
[[ "$FAIL" -eq 0 ]]
