#!/usr/bin/env bash
# Synthetic training pass for scripts/build-pgo-auto.sh -- exercises the
# instrumented (profile-generate) binaries so their .gcda files reflect
# real code paths instead of only "process started and immediately
# quit." Meant to run inside `dbus-run-session` (an isolated session
# D-Bus bus is required: the programs use the session bus (tray, portals), and without an
# isolated one they would talk to whatever real fleetwm-bar/-settings/etc is already
# running on the actual desktop session instead of doing any work themselves).
#
# Not set -e: one client failing to start shouldn't abort the whole
# training pass and skip cleanup of everything already running.
#
# What it exercises, for TRAIN_SECONDS (build-pgo-auto.sh passes 150): workspace sweeps; switching between the
# Tiling and Desktop layouts with glass on and off and in dark and light mode (theme.toml is rewritten, the
# compositor reloads it live); every Settings page; the start menu, launcher search, power menu, shortcuts
# window and language picker; Alt+Tab, snap keys, the performance overlay and layout switching by key press
# (wtype, when installed); pointer movement over the bar and windows (wlrctl, when installed); terminals
# scrolling output (foot, when installed); screen capture (grim, when installed); and the compositor's IPC
# queries. Anything missing is skipped, never an error: the more of it runs, the better the profile.
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

have() { command -v "$1" >/dev/null 2>&1; }

CONF_DIR="${HOME}/.config/fleetwm"
mkdir -p "$CONF_DIR"
END=$((SECONDS + TRAIN_SECONDS))
time_left() { (( SECONDS < END )) && kill -0 "$comp_pid" 2>/dev/null; }

# Send a command and print the (short) reply, for queries.
ipc() {
  python3 - "$IPC_SOCK" "$1" <<'PY' 2>/dev/null
import socket, sys, time
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.settimeout(0.3)
try:
    s.connect(sys.argv[1])
    s.sendall((sys.argv[2] + "\n").encode())
    time.sleep(0.05)
    print(s.recv(65536).decode(errors="replace").strip()[:200])
except Exception:
    pass
PY
}

# Rewrite theme.toml; the compositor, bar and the other programs reload it live.
#   set_theme <tiling|desktop> <glass true|false> <dark|light> [overlay true|false]
set_theme() {
  printf 'window_layout = "%s"\nglass_effects = %s\ntheme = "%s"\nshow_debug_overlay_on_startup = %s\n' \
    "$1" "$2" "$3" "${4:-false}" > "${CONF_DIR}/theme.toml.tmp" && mv "${CONF_DIR}/theme.toml.tmp" "${CONF_DIR}/theme.toml"
  sleep 1.2
}

# Key presses through a virtual keyboard (wtype); -P/-p press and release single keys, which is what makes a chord.
keys() {
  have wtype || return 0
  timeout 5 wtype "$@" >/dev/null 2>&1 || true
  sleep 0.2
}
alt_tab()       { keys -P Alt_L -P Tab -p Tab -P Tab -p Tab -P Tab -p Tab -p Alt_L; }
start_menu()    { keys -k Super_L; }
escape()        { keys -k Escape; }
snap()          { keys -P Super_L -P "$1" -p "$1" -p Super_L; }
overlay_desktop() { keys -P Control_L -P Alt_L -P i -p i -p Alt_L -p Control_L; }
overlay_tiling()  { keys -P Alt_L -P Shift_L -P i -p i -p Shift_L -p Alt_L; }
terminal_key()  { keys -P Control_L -P Alt_L -P t -p t -p Alt_L -p Control_L; }
next_layout()   { keys -P Super_L -P space -p space -p Super_L; }
focus_arrow()   { keys -P Alt_L -P "$1" -p "$1" -p Alt_L; }

# Pointer movement over the bar and the windows (wlrctl moves relative to where it is).
pointer_sweep() {
  have wlrctl || return 0
  timeout 5 wlrctl pointer move -5000 -5000 >/dev/null 2>&1
  local i
  for i in $(seq 1 14); do
    timeout 2 wlrctl pointer move $((90 + (i % 7) * 20)) $((40 + (i % 5) * 35)) >/dev/null 2>&1
    sleep 0.05
  done
  timeout 2 wlrctl pointer move -5000 5000 >/dev/null 2>&1
  for i in $(seq 1 10); do
    timeout 2 wlrctl pointer move $((60 + i * 25)) -$((i % 3 * 6)) >/dev/null 2>&1
    sleep 0.05
  done
}

# Windows opened during a round are closed again at its end (SIGTERM: the clean-quit handler lets gcov flush).
WIN_PIDS=()
open_window() {
  "$@" >/dev/null 2>&1 &
  WIN_PIDS+=("$!")
}
close_windows() {
  local p
  for p in "${WIN_PIDS[@]:-}"; do
    [[ -n "$p" ]] && kill -TERM "$p" 2>/dev/null && wait "$p" 2>/dev/null
  done
  WIN_PIDS=()
}

echo "    starting the desktop programs: bar, wallpaper, keep-awake padlock, audio mixer..."
# Locker and the greeter binaries are deliberately not included here: locker needs a real PAM round trip to
# reach its clean-unlock exit path, and the greeter is TTY/PAM-driven, not a Wayland client like the others.
spawn_client "${BUILD_DIR}/src/bar/fleetwm-bar"
spawn_client "${BUILD_DIR}/src/wallpaper/fleetwm-wallpaper"
spawn_client "${BUILD_DIR}/apps/lockapplet/fleetwm-lockapplet"
spawn_client "${BUILD_DIR}/apps/audiomixer/fleetwm-audiomixer"
sleep 2

SETTINGS_PAGES=(theme bar wallpaper display network keyboard power date default audio performance about)
round=0

phase_workspaces() {
  echo "    workspaces: sweep, queries"
  local w
  for w in 1 2 3 0 4 5 9 6 7 8 0; do
    send_ipc "WORKSPACE $w"
    sleep 0.15
  done
  ipc "WORKSPACE?" >/dev/null
  ipc "OUTPUTS?" >/dev/null
  ipc "IDLE_INHIBITORS?" >/dev/null
  ipc "LAYOUTS?" >/dev/null
}

phase_windows_and_settings() {
  echo "    windows: four Settings pages (rotating, so every page is drawn over a full run), shortcuts, language picker, terminals"
  local page k
  for k in 0 1 2 3; do
    time_left || break
    page="${SETTINGS_PAGES[$(( (round * 4 + k) % ${#SETTINGS_PAGES[@]} ))]}"
    open_window "${BUILD_DIR}/apps/settings/fleetwm-settings" --page "$page"
    sleep 0.9
    keys -k Tab -k Tab -k Down -k space
    sleep 0.2
  done
  open_window "${BUILD_DIR}/apps/shortcuts/fleetwm-shortcuts"
  open_window "${BUILD_DIR}/apps/langpicker/fleetwm-langpicker"
  if have foot; then
    open_window foot -e sh -c 'i=0; while [ $i -lt 4000 ]; do echo "fleetwm training line $i the quick brown fox jumps over the lazy dog"; i=$((i+1)); done; sleep 30'
    open_window foot
  fi
  sleep 1.5
  ipc "FOCUSED_TITLE" >/dev/null
  have grim && timeout 10 grim "${RUNTIME_DIR}/shot.png" >/dev/null 2>&1
  alt_tab
  pointer_sweep
  snap Left; snap Right; snap Up; snap Down
  focus_arrow Right; focus_arrow Left
  ipc "WINDOW_ACTIVATE 0" >/dev/null
  sleep 0.5
  close_windows
}

phase_menus() {
  echo "    menus: start menu, launcher search, power menu, overlay"
  start_menu; sleep 0.7
  keys "fo"; sleep 0.3
  keys -k Down -k Down -k Up; sleep 0.3
  escape; sleep 0.4
  start_menu; sleep 0.5; start_menu; sleep 0.4
  open_window "${BUILD_DIR}/apps/launcher/fleetwm-launcher"
  sleep 0.8; keys "set"; sleep 0.3; escape; sleep 0.2
  open_window "${BUILD_DIR}/apps/powermenu/fleetwm-powermenu"
  sleep 0.8; keys -k Down -k Down -k Down; sleep 0.2; escape; sleep 0.2
  overlay_desktop; overlay_tiling; sleep 0.8
  have grim && timeout 10 grim "${RUNTIME_DIR}/shot.png" >/dev/null 2>&1
  overlay_desktop; overlay_tiling
  close_windows
}

phase_ipc_and_layouts() {
  echo "    layouts: keyboard layout, idle inhibit, window queries"
  next_layout
  ipc "LAYOUT_NEXT" >/dev/null
  ipc "LAYOUTS?" >/dev/null
  send_ipc "IDLE_INHIBIT 1"; sleep 0.3; send_ipc "IDLE_INHIBIT 0"
  open_window "${BUILD_DIR}/apps/settings/fleetwm-settings" --page theme
  open_window "${BUILD_DIR}/apps/shortcuts/fleetwm-shortcuts"
  sleep 1.2
  ipc "WINDOW_ACTIVATE 1" >/dev/null
  ipc "WINDOW_MINIMIZE 0" >/dev/null
  ipc "WINDOW_TOGGLE 0" >/dev/null
  sleep 0.4
  ipc "WINDOW_CLOSE 1" >/dev/null
  close_windows
}

# One configuration per round, so over a full run every layout, glass and colour combination is drawn.
COMBOS=("desktop true dark" "tiling false dark" "desktop false light" "tiling true light" "desktop true light" "tiling true dark")
echo "    running for about ${TRAIN_SECONDS}s: ${#COMBOS[@]} look/layout combinations in turn, each with the full workload..."
while time_left; do
  combo=(${COMBOS[$((round % ${#COMBOS[@]}))]})
  round=$((round + 1))
  echo "  round ${round}: ${combo[0]} layout, glass ${combo[1]}, ${combo[2]} mode ($(( END - SECONDS ))s left)"
  set_theme "${combo[0]}" "${combo[1]}" "${combo[2]}"
  phase_workspaces
  time_left && phase_windows_and_settings
  time_left && phase_menus
  time_left && phase_ipc_and_layouts
  # Flip the layout while windows are open, so the relayout and snap paths run with real content.
  if time_left; then
    open_window "${BUILD_DIR}/apps/settings/fleetwm-settings" --page bar
    open_window "${BUILD_DIR}/apps/shortcuts/fleetwm-shortcuts"
    sleep 1
    if [[ "${combo[0]}" == tiling ]]; then set_theme desktop "${combo[1]}" "${combo[2]}"; else set_theme tiling "${combo[1]}" "${combo[2]}"; fi
    sleep 1; close_windows
  fi
done
(( round > 0 )) || echo "    the compositor stopped early (see ${COMP_LOG})" >&2

echo "    final workspace switches..."
send_ipc "WORKSPACE 1"
send_ipc "WORKSPACE 0"

echo "    training pass complete after ${round} rounds, shutting everything down cleanly..."
# cleanup() (the EXIT trap) does the actual SIGTERM + wait for every
# child, compositor last.
