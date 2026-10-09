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
# What it exercises, for TRAIN_SECONDS (build-pgo-auto.sh passes its default, see there):
#   * every look, round by round: the Tiling and Desktop layouts, glass on and off, the five themes (dark, light,
#     catppuccin, dracula, oled_black), square and rounded corners, window frames of 0, 6, 8 and 12 px, titlebar
#     button sides and title alignments, the three bar layouts (full, island, capsules), the taskbar on all four
#     edges, the three power modes and the seconds clock (theme.toml and bar.toml are rewritten, the programs reload
#     them live);
#   * every icon in every state: the battery (charging and sweeping, full, under 10%, mid charge, none), the network
#     (Wi-Fi up and down, wired up and unplugged, mobile data up and down, no card), the volume (every number of waves
#     and mute, from a real PipeWire null sink when pipewire and wireplumber are installed) and the power-mode gauge:
#     extra bars are started with fake sysfs trees (FLEETWM_BATTERY_DIR, FLEETWM_SYS_NET), several at once;
#   * windows and their frames: opening, cascade, focus and unfocus, hover over every caption button, pin, minimize,
#     maximize and restore by button and by double click, drags by the titlebar, resizes by every edge and corner,
#     snaps by key and by dragging to the screen edges, keyboard maximize, close; in the Tiling layout focus
#     borders, pinned borders, floating, focus by key and by pointer (a small virtual pointer, scripts/pgo-pointer.c,
#     built on the fly; without it only the keyboard and wlrctl parts run);
#   * every virtual desktop (workspaces 0-9) with windows on several of them, send-to-workspace, next and previous;
#   * every Settings page, the start menu, launcher search, power menu, shortcuts window and language picker,
#     Alt+Tab, the performance overlay, keyboard layout switching, idle inhibit, screen capture (grim), terminals
#     scrolling output (foot), a change of screen size to 1024x768 and the compositor's IPC queries.
# Anything missing is skipped, never an error: the more of it runs, the better the profile.
set -uo pipefail

RUNTIME_DIR="$1"
TRAIN_SECONDS="$2"
BUILD_DIR="$3"

IPC_SOCK="${RUNTIME_DIR}/fleetwm.sock"
COMP_LOG="${RUNTIME_DIR}/compositor.log"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

CHILD_PIDS=()

cleanup() {
  # Reverse order: clients before the compositor they depend on. Every
  # binary here calls fleetwm::install_clean_quit() (src/common/
  # clean_quit.cpp), so SIGTERM reaches g_application_quit()/
  # wl_display_terminate() -- not the kernel's raw "terminate
  # immediately" default -- which is what lets gcov's atexit-based
  # flush actually run and write real .gcda data.
  local p
  for p in "${EXTRA_PIDS[@]:-}" "${WIN_PIDS[@]:-}"; do
    [[ -n "$p" ]] && kill -TERM "$p" 2>/dev/null
  done
  for ((i = ${#CHILD_PIDS[@]} - 1; i >= 0; i--)); do
    pid="${CHILD_PIDS[$i]}"
    kill -TERM "$pid" 2>/dev/null || continue
    wait "$pid" 2>/dev/null || true
  done
}
EXTRA_PIDS=()
WIN_PIDS=()
trap cleanup EXIT

# Every program found by name from here on is the instrumented one from the build directory, not an old
# installed copy (which would record nothing) or nothing at all (see scripts/pgo-path-shim.sh).
SHIM_DIR="${RUNTIME_DIR}/bin"
bash "$(dirname "${BASH_SOURCE[0]}")/pgo-path-shim.sh" "$BUILD_DIR" "$SHIM_DIR"
export PATH="${SHIM_DIR}:${PATH}"

have() { command -v "$1" >/dev/null 2>&1; }

# ---- fake machine state: battery and network trees the bar reads (test hooks of the bar and of netmgr) -------------
FAKE="${RUNTIME_DIR}/fake"
mkdir -p "$FAKE"

# make_bat DIR PERCENT STATUS  (a sysfs battery directory)
make_bat() {
  mkdir -p "$1" && printf '%s\n' "$2" > "$1/capacity" && printf '%s\n' "$3" > "$1/status"
}
# make_nic DIR NAME KIND STATE   KIND: wired|wifi|mobile   STATE: up|down|unplugged
make_nic() {
  local d="$1/$2"
  mkdir -p "$d/device"
  printf '1\n' > "$d/type"
  printf 'aa:bb:cc:00:00:%02x\n' $((RANDOM % 200)) > "$d/address"
  case "$4" in
    up) printf 'up\n' > "$d/operstate"; printf '1\n' > "$d/carrier" ;;
    down) printf 'down\n' > "$d/operstate"; printf '1\n' > "$d/carrier" ;;
    *) printf 'down\n' > "$d/operstate"; printf '0\n' > "$d/carrier" ;;
  esac
  case "$3" in
    wifi) mkdir -p "$d/wireless" ;;
    wired) printf '1000\n' > "$d/speed" ;;
    mobile) rm -f "$d/carrier" ;;
  esac
}

# The battery and network the autostarted bar sees: charging at 64%, wired and Wi-Fi up.
make_bat "$FAKE/bat-main" 64 Charging
make_nic "$FAKE/net-main" eth0 wired up
make_nic "$FAKE/net-main" wlan0 wifi up
export FLEETWM_BATTERY_DIR="$FAKE/bat-main"
export FLEETWM_SYS_NET="$FAKE/net-main"

# ---- sound: a PipeWire null sink, so the volume icon and the mixer have a real volume to show ---------------------
AUDIO=0
start_audio() {
  have pipewire && have wireplumber && have wpctl && have pw-cli || return 0
  pipewire >"${RUNTIME_DIR}/pipewire.log" 2>&1 &
  CHILD_PIDS+=("$!")
  sleep 0.5
  wireplumber >"${RUNTIME_DIR}/wireplumber.log" 2>&1 &
  CHILD_PIDS+=("$!")
  sleep 0.8
  pw-cli create-node adapter '{ factory.name=support.null-audio-sink node.name=train-sink media.class=Audio/Sink object.linger=true audio.position=[FL FR] }' >/dev/null 2>&1 && AUDIO=1
  sleep 0.4
}
start_audio

# ---- the pointer helper (virtual pointer: drags, double clicks, wheel) -------------------------------------------
PTR=""
build_pointer() {
  have wayland-scanner && have cc || return 0
  local d="${RUNTIME_DIR}/ptr"
  mkdir -p "$d"
  wayland-scanner client-header "${SCRIPT_DIR}/wlr-virtual-pointer-unstable-v1.xml" "$d/wlr-virtual-pointer-unstable-v1-client-protocol.h" 2>/dev/null &&
    wayland-scanner private-code "${SCRIPT_DIR}/wlr-virtual-pointer-unstable-v1.xml" "$d/wlr-vp-code.c" 2>/dev/null &&
    cc -O1 -w -I"$d" -o "$d/pgo-pointer" "${SCRIPT_DIR}/pgo-pointer.c" "$d/wlr-vp-code.c" -lwayland-client 2>/dev/null &&
    PTR="$d/pgo-pointer"
}
build_pointer
ptr() { [[ -n "$PTR" ]] && timeout 15 "$PTR" "$@" >/dev/null 2>&1; return 0; }

echo "    starting instrumented compositor (headless backend)..."
"${BUILD_DIR}/src/compositor/fleetwm" >"$COMP_LOG" 2>&1 &
comp_pid=$!
CHILD_PIDS+=("$comp_pid")

for _ in $(seq 1 100); do
  [[ -S "$IPC_SOCK" ]] && break
  if ! kill -0 "$comp_pid" 2>/dev/null; then
    echo "    compositor exited before opening its IPC socket:" >&2
    cat "$COMP_LOG" >&2
    exit 1
  fi
  sleep 0.1
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
  # $1 = binary path; each client keeps running independently in the background until the final cleanup pass.
  "$1" >"${RUNTIME_DIR}/$(basename "$1").log" 2>&1 &
  CHILD_PIDS+=("$!")
}

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
    time.sleep(0.03)
    print(s.recv(65536).decode(errors="replace").strip()[:2000])
except Exception:
    pass
PY
}

# Window ids, in the compositor's order, from the same snapshot the taskbar gets.
win_ids() {
  ipc "SUBSCRIBE_WINDOWS" | python3 -c '
import sys
f = sys.stdin.read().strip().split("\t")
print(" ".join(f[i] for i in range(1, len(f), 5)))' 2>/dev/null
}
win_count() { local ids; ids="$(win_ids)"; set -- $ids; echo $#; }
# Wait until at least $1 windows exist (at most $2 seconds), instead of sleeping a fixed time.
wait_windows() {
  local want="$1" limit=$(( ${2:-4} * 10 )) i
  for ((i = 0; i < limit; i++)); do
    (( $(win_count) >= want )) && return 0
    sleep 0.1
  done
  return 1
}
first_win() { set -- $(win_ids); echo "${1:-0}"; }
last_win() { local ids; ids="$(win_ids)"; set -- $ids; eval "echo \${$#:-0}"; }

# Rewrite theme.toml and bar.toml; the compositor, bar and the other programs reload them live.
#   set_look <layout> <glass> <theme> <overlay> <corner> <frame_px> <buttons_side> <title_align>
set_look() {
  printf 'window_layout = "%s"\nglass_effects = %s\ntheme = "%s"\nshow_debug_overlay_on_startup = %s\ncorner_style = "%s"\n[titlebar]\nframe_px = %s\nbuttons_side = "%s"\ntitle_align = "%s"\n' \
    "$1" "$2" "$3" "$4" "$5" "$6" "$7" "$8" > "${CONF_DIR}/theme.toml.tmp" && mv "${CONF_DIR}/theme.toml.tmp" "${CONF_DIR}/theme.toml"
  sleep 0.45
}
#   set_bar <layout> <power_mode> <taskbar_position> <seconds true|false>
set_bar() {
  printf 'layout = "%s"\npower_mode = "%s"\ntaskbar_position = "%s"\n[clock]\nshow_seconds = %s\nshow_date = true\nuse_24h = true\n' \
    "$1" "$2" "$3" "$4" > "${CONF_DIR}/bar.toml.tmp" && mv "${CONF_DIR}/bar.toml.tmp" "${CONF_DIR}/bar.toml"
  sleep 0.35
}

# Key presses through a virtual keyboard (wtype); -P/-p press and release single keys, which is what makes a chord.
keys() {
  have wtype || return 0
  timeout 5 wtype "$@" >/dev/null 2>&1 || true
  sleep 0.08
}
# Chords use wtype's modifier options (-M presses a modifier, -m releases it): pressing the modifier as an ordinary key
# (-P Super_L) leaves the modifier state empty and the compositor never sees a shortcut.
alt_tab()       { keys -M alt -k Tab -k Tab -k Tab -m alt; }
start_menu()    { keys -k Super_L; }
escape()        { keys -k Escape; }
snap()          { keys -M logo -k "$1" -m logo; }
overlay_desktop() { keys -M ctrl -M alt -k i -m alt -m ctrl; }
overlay_tiling()  { keys -M alt -M shift -k i -m shift -m alt; }
terminal_key()  { keys -M ctrl -M alt -k t -m alt -m ctrl; }
next_layout()   { keys -M logo -k space -m logo; }
focus_arrow()   { keys -M alt -k "$1" -m alt; }
maximize_key()  { keys -M alt -k F10 -m alt; }
tiling_key()    { keys -M alt -k "$1" -m alt; }                  # Alt + key (focus, terminal, ...)
tiling_shift()  { keys -M alt -M shift -k "$1" -m shift -m alt; }  # Alt + Shift + key (pin, float, close)
ws_key()        { keys -M logo -k "$1" -m logo; }                # Super + digit: go to that workspace
ws_send_key()   { keys -M logo -M shift -k "$1" -m shift -m logo; }  # Super + Shift + digit: send the window there
ws_next()       { keys -M ctrl -M alt -k Right -m alt -m ctrl; }
ws_prev()       { keys -M ctrl -M alt -k Left -m alt -m ctrl; }
show_desktop()  { keys -M logo -k d -m logo; }
minimize_all()  { keys -M logo -k m -m logo; }
restore_all()   { keys -M logo -M shift -k m -m shift -m logo; }
close_key()     { keys -M alt -k F4 -m alt; }

# Pointer movement over the bar and the windows (wlrctl moves relative to where it is).
pointer_sweep() {
  have wlrctl || return 0
  timeout 5 wlrctl pointer move -5000 -5000 >/dev/null 2>&1
  local i
  for i in $(seq 1 14); do
    timeout 2 wlrctl pointer move $((90 + (i % 7) * 20)) $((40 + (i % 5) * 35)) >/dev/null 2>&1
  done
  timeout 2 wlrctl pointer move -5000 5000 >/dev/null 2>&1
  for i in $(seq 1 10); do
    timeout 2 wlrctl pointer move $((60 + i * 25)) -$((i % 3 * 6)) >/dev/null 2>&1
  done
}

# Windows opened during a round are closed again at its end (SIGTERM: the clean-quit handler lets gcov flush).
WIN_NAMES=()
open_window() {
  "$@" >/dev/null 2>&1 &
  WIN_PIDS+=("$!")
  WIN_NAMES+=("$*")
}
close_windows() {
  local p i rc
  for p in "${WIN_PIDS[@]:-}"; do
    [[ -n "$p" ]] && kill -TERM "$p" 2>/dev/null
  done
  for i in "${!WIN_PIDS[@]}"; do
    p="${WIN_PIDS[$i]}"
    [[ -n "$p" ]] || continue
    wait "$p" 2>/dev/null
    rc=$?
    # 128+signal: one of our own programs died (a crash is worth knowing about, and the profile of a crashed program is lost)
    [[ "${WIN_NAMES[$i]}" == "${BUILD_DIR}"* ]] && (( rc >= 128 && rc != 143 )) && echo "    WARNING: ${WIN_NAMES[$i]} ended with status ${rc}" | tee -a "${RUNTIME_DIR}/crashes.log"
  done
  WIN_PIDS=()
  WIN_NAMES=()
}
# A screenshot for debugging the training itself: PGO_SHOTS=<dir> saves one after the steps that call it.
shot() {
  [[ -n "${PGO_SHOTS:-}" ]] && have grim && timeout 10 grim "${PGO_SHOTS}/$1.png" >/dev/null 2>&1
  return 0
}

echo "    making sure the desktop programs run: bar, wallpaper, keep-awake padlock, audio mixer..."
# Locker and the greeter binaries are deliberately not included here: locker needs a real PAM round trip to
# reach its clean-unlock exit path, and the greeter is TTY/PAM-driven, not a Wayland client like the others.
# The compositor autostarts the bar, the wallpaper and the padlock itself (by name, so the instrumented copies
# through the PATH above). Start any of them it did not, so they always run, and never twice.
sleep 1
is_running() {  # is a process running this exact executable?
  local p
  for p in /proc/[0-9]*; do
    [[ "$(readlink "$p/exe" 2>/dev/null)" == "$1" ]] && return 0
  done
  return 1
}
ensure_running() {
  local exe
  exe="$(readlink -f "$1")"
  is_running "$exe" || spawn_client "$1"
}
ensure_running "${BUILD_DIR}/src/bar/fleetwm-bar"
ensure_running "${BUILD_DIR}/src/wallpaper/fleetwm-wallpaper"
ensure_running "${BUILD_DIR}/apps/lockapplet/fleetwm-lockapplet"
ensure_running "${BUILD_DIR}/apps/desktop/fleetwm-desktop"
spawn_client "${BUILD_DIR}/apps/audiomixer/fleetwm-audiomixer"
sleep 0.6

SETTINGS_PAGES=(theme bar wallpaper display network keyboard mouse power date default audio performance about)
round=0

# ---- virtual desktops: windows on several of them, every workspace visited, windows sent between them ----------------
phase_workspaces() {
  echo "    workspaces: windows on 1-4, every workspace 0-9 visited, send, next, previous, queries"
  local w
  send_ipc "WORKSPACE 1"
  open_window foot
  open_window "${BUILD_DIR}/apps/shortcuts/fleetwm-shortcuts"
  wait_windows 2 3
  for w in 2 3 4; do
    send_ipc "WORKSPACE $w"
    sleep 0.1
    if (( w == 2 )); then open_window "${BUILD_DIR}/apps/settings/fleetwm-settings" --page bar; fi
    if (( w == 3 )); then open_window foot; fi
    if (( w == 4 )); then open_window "${BUILD_DIR}/apps/langpicker/fleetwm-langpicker"; fi
    wait_windows $((w + 1)) 3
  done
  for w in 1 2 3 0 4 5 9 6 7 8 0 1; do
    send_ipc "WORKSPACE $w"
    sleep 0.08
  done
  ws_key 2; ws_send_key 3; ws_key 3; ws_send_key 1; ws_next; ws_next; ws_prev
  for w in 4 3 2 1; do ws_key "$w"; done
  ipc "WORKSPACE?" >/dev/null
  ipc "OUTPUTS?" >/dev/null
  ipc "IDLE_INHIBITORS?" >/dev/null
  ipc "LAYOUTS?" >/dev/null
  show_desktop; sleep 0.15; show_desktop
  minimize_all; sleep 0.15; restore_all
  send_ipc "WORKSPACE 0"
  close_windows
}

# ---- one window's caption: find the close button on screen, then hover and click the others relative to it --------------
#   caption_at <xmin> <xmax>  prints "x y", the centre of the red close button inside that screen span ("" if none)
caption_at() {
  have grim || return 0
  timeout 10 grim -t ppm -g "$1,0 $(( $2 - $1 ))x90" - 2>/dev/null | python3 -c '
import sys
d = sys.stdin.buffer.read()
try:
    parts = d.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
except Exception:
    sys.exit(0)
xs = ys = 0; n = 0
for y in range(h):
    row = px[y * w * 3:(y + 1) * w * 3]
    for x in range(w):
        r, g, b = row[3 * x], row[3 * x + 1], row[3 * x + 2]
        if r > 150 and g < 90 and b < 90:
            xs += x; ys += y; n += 1
if n > 40:
    print(xs // n + int(sys.argv[1]), ys // n)' "$1" 2>/dev/null
}
#   caption_buttons <close_x> <y>: the strip is pin, minimize, maximize, close, 38-49 px apart
caption_buttons() {
  local cx="$1" y="$2" x
  [[ -n "$cx" && -n "$y" ]] || return 0
  for x in $(seq $((cx - 170)) 14 $((cx + 22))); do ptr abs "$x" "$y"; done   # hover over every button in turn
  ptr click $((cx - 125)) "$y"   # pin
  ptr click $((cx - 125)) "$y"   # unpin
  ptr abs $((cx - 5)) "$y"; ptr abs 300 300
}

# ---- window elements: move, resize by every edge and corner, snap by dragging, maximize, minimize -----------------
phase_window_elements() {
  have wtype || [[ -n "$PTR" ]] || return 0
  echo "    window elements: caption buttons, drags, edges and corners, snaps, maximize, minimize, focus"
  local f="$1" top="$2" ty id cx cy
  open_window "${BUILD_DIR}/apps/settings/fleetwm-settings" --page theme
  wait_windows 1 4
  sleep 0.2
  shot 01-opened
  snap Left; sleep 0.2                        # known place: the left half of the work area (640 x 676 at 1280x720)
  ty=$((top + f + 16))
  read -r cx cy < <(caption_at 0 640)
  shot 02-snapped-left
  caption_buttons "$cx" "$cy"
  open_window "${BUILD_DIR}/apps/shortcuts/fleetwm-shortcuts"
  wait_windows 2 3
  sleep 0.2
  snap Right; sleep 0.2                       # the second window takes the right half, the first one loses focus
  shot 03-both-snapped
  ptr click 320 "$ty"; sleep 0.1              # click the first window's titlebar: focus changes, frames repaint
  ptr click 960 "$ty"; sleep 0.1
  read -r cx cy < <(caption_at 640 1280)
  caption_buttons "$cx" "$cy"
  ptr abs 640 300; ptr abs 639 300; ptr abs 641 300; ptr abs 5 300
  ptr click 320 "$ty"; sleep 0.1
  # edges and corners of the left window (its right edge is at 639, its bottom edge above the taskbar)
  ptr drag 639 $((top + 300)) 700 $((top + 300)) 3      # right edge, wider
  ptr drag 5 $((top + 300)) 40 $((top + 300)) 3         # left edge (the left half starts at x=0)
  shot 04-edges
  ptr drag 300 $((top + 674)) 300 $((top + 600)) 3      # bottom edge, shorter (the bar sits below)
  ptr drag 300 $((top + 1)) 300 $((top + 30)) 3         # top edge
  ptr drag 700 $((top + 600)) 740 $((top + 640)) 3      # bottom-right corner, larger again
  ptr drag 41 $((top + 31)) 20 $((top + 10)) 3          # top-left corner, larger
  ptr drag 740 $((top + 31)) 700 $((top + 60)) 3        # top-right corner
  ptr drag 21 $((top + 640)) 50 $((top + 600)) 3        # bottom-left corner
  shot 05-corners
  # move by the titlebar, and snap by dragging to every edge and corner of the screen
  ptr drag 300 "$ty" 500 300 4; sleep 0.1
  shot 06-moved
  ptr drag 500 300 3 400 5; sleep 0.15                  # left edge: snap left
  ptr drag 300 "$ty" 1277 400 6; sleep 0.15             # right edge: snap right
  shot 07-drag-snap
  ptr drag 960 "$ty" 640 $((top + 1)) 5; sleep 0.15     # top edge: maximize
  shot 08-drag-maximize
  ptr dclick 640 "$ty"; sleep 0.2                        # double click: restore
  ptr drag 640 "$ty" 2 $((top + 2)) 5; sleep 0.15       # top-left corner: quarter
  ptr drag 160 "$ty" 1277 $((top + 3)) 5; sleep 0.15    # top-right corner
  ptr drag 960 "$ty" 2 710 5; sleep 0.15                # bottom-left corner
  ptr drag 160 "$ty" 1277 710 5; sleep 0.15             # bottom-right corner
  shot 09-quarters
  ptr dclick 960 "$ty"; sleep 0.2                        # maximize by double click
  shot 10-dclick
  ptr dclick 640 "$ty"; sleep 0.2
  ptr rclick 400 "$ty"; escape
  # the caption buttons themselves: maximize, restore, minimize, then back from the taskbar
  snap Left; sleep 0.2
  read -r cx cy < <(caption_at 0 640)
  if [[ -n "$cx" ]]; then
    ptr click $((cx - 49)) "$cy"; sleep 0.25               # maximize button
    shot 11-maximized
    read -r cx cy < <(caption_at 0 1280)
    [[ -n "$cx" ]] && { ptr click $((cx - 49)) "$cy"; sleep 0.25; }   # restore
    shot 12-restored
    read -r cx cy < <(caption_at 0 1280)
    [[ -n "$cx" ]] && { ptr click $((cx - 87)) "$cy"; sleep 0.3; }    # minimize button
    shot 13-minimized
  fi
  maximize_key; sleep 0.2; maximize_key; sleep 0.2
  snap Up; sleep 0.2; snap Down; sleep 0.2; snap Left; sleep 0.2
  id="$(first_win)"; send_ipc "WINDOW_ACTIVATE $id"; sleep 0.2
  id="$(last_win)"; send_ipc "WINDOW_MINIMIZE $id"; sleep 0.15; send_ipc "WINDOW_TOGGLE $id"; sleep 0.15
  alt_tab
  close_key; sleep 0.2                                   # close the focused window by key
  close_windows
}

# ---- the desktop taskbar with the pointer: start button, pager, window buttons, tooltips, the status icons -----------
#   taskbar_pointer <y>   y of the taskbar's middle (698 at the bottom, 22 at the top of a 1280x720 screen)
phase_taskbar_pointer() {
  [[ -n "$PTR" ]] || return 0
  echo "    taskbar: start button, workspaces, window buttons, tooltips on every status icon, clicks"
  local y="$1" x
  open_window "${BUILD_DIR}/apps/settings/fleetwm-settings" --page power
  open_window "${BUILD_DIR}/apps/shortcuts/fleetwm-shortcuts"
  wait_windows 2 3
  sleep 0.2
  ptr click 28 "$y"; sleep 0.25; ptr click 28 "$y"; sleep 0.1         # start button: open, close
  for x in 74 104 133 162 74; do ptr click "$x" "$y"; sleep 0.05; done  # workspace buttons
  for x in 240 420 240 420; do ptr click "$x" "$y"; sleep 0.08; done    # window buttons: activate, minimize, restore
  # tooltips need the pointer to rest for 400 ms: window button, CPU, layout, volume, network, power mode, battery, clock
  for x in 240 850 965 1008 1046 1083 1130 1240; do ptr abs "$x" "$y"; sleep 0.45; done
  # the stats grid: the tooltips of CPU, RAM, GPU and disk redraw once a second while they are shown, so rest 1.5 s on each
  for xy in "850 $((y - 11))" "915 $((y - 11))" "850 $((y + 11))" "915 $((y + 11))"; do ptr abs ${xy}; sleep 1.5; done
  ptr click 965 "$y"; sleep 0.1                                         # next keyboard layout
  ptr click 1046 "$y"; sleep 0.3                                        # network: opens Settings on its page
  ptr click 1130 "$y"; sleep 0.2                                        # battery: opens Settings on the Power page
  ptr click 1008 "$y"; sleep 0.3                                        # volume: opens the mixer
  ptr rclick 1008 "$y"; sleep 0.1
  ptr scroll 3; ptr scroll -3
  ptr abs 640 300
  close_windows
}

# ---- tiling layout: borders for focus, pin and float, focus by key and pointer, moving between workspaces --------
phase_tiling() {
  have wtype || return 0
  echo "    tiling: three windows, focus and pinned borders, float, close, send to workspace"
  open_window foot
  open_window "${BUILD_DIR}/apps/settings/fleetwm-settings" --page bar
  open_window "${BUILD_DIR}/apps/shortcuts/fleetwm-shortcuts"
  wait_windows 3 4
  sleep 0.2
  focus_arrow h; focus_arrow l; focus_arrow j; focus_arrow k
  tiling_shift p; sleep 0.15                             # pin: pinned border colour
  focus_arrow h; focus_arrow l
  tiling_shift p; sleep 0.1
  tiling_shift f; sleep 0.15                             # float
  tiling_shift f; sleep 0.1
  ptr abs 200 200; ptr abs 640 300; ptr abs 1000 400; ptr abs 200 500   # focus follows the pointer in tiling
  tiling_key Return; sleep 0.3                           # a terminal
  ws_send_key 2; ws_key 2; ws_key 1
  tiling_shift q; sleep 0.2                              # close the focused window
  send_ipc "WORKSPACE 1"
  close_windows
}

# ---- every icon in every state: extra bars, several at once, each with its own fake battery and network ----------
#   One bar per state, started together and stopped together, so the whole sweep costs about two seconds. A bar reads
#   the battery and the network when it starts, and animates the charging fill while it charges, so two seconds are
#   enough for the sweep (a 500 ms step each). The Settings network page draws the big icons for two of the states.
icon_n=0
phase_icons() {
  echo "    icons: battery, network and volume in every state"
  local k=0 b n pid
  EXTRA_PIDS=()
  icon_state() {  # icon_state <battery percent|none> <status> <nic specs...>
    local bat="$1" status="$2"; shift 2
    k=$((k + 1))
    local bd="$FAKE/bat-$k" nd="$FAKE/net-$k"
    rm -rf "$bd" "$nd"; mkdir -p "$nd"
    [[ "$bat" == none ]] && bd="$FAKE/no-battery" || make_bat "$bd" "$bat" "$status"
    local spec
    for spec in "$@"; do make_nic "$nd" "${spec%%:*}" "$(echo "$spec" | cut -d: -f2)" "$(echo "$spec" | cut -d: -f3)"; done
    FLEETWM_BATTERY_DIR="$bd" FLEETWM_SYS_NET="$nd" "${BUILD_DIR}/src/bar/fleetwm-bar" >"${RUNTIME_DIR}/bar-extra-$k.log" 2>&1 &
    EXTRA_PIDS+=("$!")
  }
  icon_state 40 Charging wlan0:wifi:up eth0:wired:unplugged       # sweeping fill, Wi-Fi, no cable
  icon_state 100 Full eth0:wired:up                                 # green full battery, wired
  icon_state 8 Discharging wwan0:mobile:up                          # red under 10%, mobile data
  icon_state 55 Discharging wlan0:wifi:down wwan0:mobile:down       # Wi-Fi down, modem down
  icon_state 5 Charging                                             # red and charging, no network card
  icon_state none Unknown eth0:wired:unplugged wlan0:wifi:up        # no battery at all
  icon_state 100 Charging wlan0:wifi:down eth0:wired:up
  icon_state 25 Discharging wlan0:wifi:up wwan0:mobile:up
  # The big icons on the Settings network page, for a Wi-Fi card, a cable and a modem.
  FLEETWM_SYS_NET="$FAKE/net-1" open_window "${BUILD_DIR}/apps/settings/fleetwm-settings" --page network
  FLEETWM_SYS_NET="$FAKE/net-3" open_window "${BUILD_DIR}/apps/settings/fleetwm-settings" --page network
  # The wpa_supplicant back end (used when NetworkManager is absent) against a pretend supplicant, which stops by itself.
  if have python3; then
    python3 "${SCRIPT_DIR}/pgo-fake-wpa.py" "$FAKE/wpa" 7 >/dev/null 2>&1 &
    CHILD_PIDS+=("$!")
    sleep 0.4
    FLEETWM_WPA_DIR="$FAKE/wpa" FLEETWM_SYS_NET="$FAKE/net-1" open_window "${BUILD_DIR}/apps/settings/fleetwm-settings" --page network
    sleep 2.5
  fi
  # Volume: every number of waves, then muted, on the real bar through the PipeWire null sink.
  if (( AUDIO )); then
    local v
    for v in 0.00 0.20 0.50 0.90 1.00 0.35; do
      wpctl set-volume @DEFAULT_AUDIO_SINK@ "$v" >/dev/null 2>&1
      sleep 0.12
    done
    wpctl set-mute @DEFAULT_AUDIO_SINK@ 1 >/dev/null 2>&1; sleep 0.12
    wpctl set-mute @DEFAULT_AUDIO_SINK@ 0 >/dev/null 2>&1; sleep 0.12
    ptr click 1000 700   # opens the mixer from the bar's volume icon (the position is only a guess)
  else
    sleep 1.4
  fi
  sleep 0.6
  for pid in "${EXTRA_PIDS[@]}"; do kill -TERM "$pid" 2>/dev/null; done
  for pid in "${EXTRA_PIDS[@]}"; do wait "$pid" 2>/dev/null; done
  EXTRA_PIDS=()
  close_windows
  icon_n=$((icon_n + 1))
}

settings_visits=0
phase_windows_and_settings() {
  settings_visits=$((settings_visits + 1))
  echo "    windows: five Settings pages (rotating, so every page is drawn over a full run), shortcuts, language picker, terminals"
  local page k
  for k in 0 1 2 3 4; do
    time_left || break
    page="${SETTINGS_PAGES[$(( (settings_visits * 5 + k) % ${#SETTINGS_PAGES[@]} ))]}"
    open_window "${BUILD_DIR}/apps/settings/fleetwm-settings" --page "$page"
    wait_windows $((k + 1)) 3
    keys -k Tab -k Tab -k Down -k space
  done
  open_window "${BUILD_DIR}/apps/shortcuts/fleetwm-shortcuts"
  open_window "${BUILD_DIR}/apps/langpicker/fleetwm-langpicker"
  if have foot; then
    open_window foot -e sh -c 'i=0; while [ $i -lt 4000 ]; do echo "fleetwm training line $i the quick brown fox jumps over the lazy dog"; i=$((i+1)); done; sleep 30'
    open_window foot
  fi
  wait_windows 6 3
  sleep 0.4
  ipc "FOCUSED_TITLE" >/dev/null
  have grim && timeout 10 grim "${RUNTIME_DIR}/shot.png" >/dev/null 2>&1
  alt_tab
  pointer_sweep
  snap Left; snap Right; snap Up; snap Down
  focus_arrow Right; focus_arrow Left
  ipc "WINDOW_ACTIVATE 0" >/dev/null
  sleep 0.2
  close_windows
}

phase_menus() {
  echo "    menus: start menu, launcher search, power menu, overlay"
  start_menu; sleep 0.35
  keys "fo"; sleep 0.1
  keys -k Down -k Down -k Up; sleep 0.15
  escape; sleep 0.1
  start_menu; sleep 0.2; start_menu; sleep 0.1
  open_window "${BUILD_DIR}/apps/launcher/fleetwm-launcher"
  sleep 0.35; keys "set"; sleep 0.1; escape; sleep 0.05
  open_window "${BUILD_DIR}/apps/powermenu/fleetwm-powermenu"
  sleep 0.35; keys -k Down -k Down -k Down; sleep 0.05; escape; sleep 0.05
  overlay_desktop; overlay_tiling; sleep 0.3
  have grim && timeout 10 grim "${RUNTIME_DIR}/shot.png" >/dev/null 2>&1
  overlay_desktop; overlay_tiling
  close_windows
}

# The file manager: a scripted run of the whole window without a screen (every style, view, search, verified copy, undo, dialogs; about two
# seconds, `fleetwm-fm --train`), then a real window on a folder, and on the desktop the Windows 7 menu of fleetwm-desktop: the View submenu,
# an icon's menu, a double click on an icon (which starts the file manager). Needs nothing the other phases do not.
phase_files_and_desktop() {
  echo "    file manager and desktop: scripted run, a real window, the desktop menus"
  timeout 90 "${BUILD_DIR}/apps/fleetfm/fleetwm-fm" --train "${RUNTIME_DIR}/fmtrain" >/dev/null 2>&1 || echo "    WARNING: fleetwm-fm --train failed" | tee -a "${RUNTIME_DIR}/crashes.log"
  mkdir -p "${HOME}/Desktop" "${RUNTIME_DIR}/fmwork/sub"
  local f
  for f in notes.txt photo.png report.pdf song.mp3; do : > "${HOME}/Desktop/$f"; : > "${RUNTIME_DIR}/fmwork/$f"; done
  open_window "${BUILD_DIR}/apps/fleetfm/fleetwm-fm" "${RUNTIME_DIR}/fmwork"
  wait_windows 1 3
  sleep 0.5
  keys "n"; keys -k Down -k Down -k Up; keys -k F2; keys -k Escape
  keys -M ctrl -k t -m ctrl; sleep 0.2; keys -M ctrl -k w -m ctrl
  keys -M ctrl -k h -m ctrl; keys -M ctrl -k h -m ctrl
  keys -M alt -k Left -m alt; sleep 0.1
  ptr rclick 640 300; sleep 0.2; escape
  sleep 0.2
  close_windows
  # the desktop (Desktop layout rounds only: in the Tiling layout the program shows just the shortcut card)
  if [[ "$1" == desktop ]]; then
    sleep 0.3
    ptr rclick 900 500; sleep 0.25
    ptr abs 930 516; sleep 0.2; ptr abs 1000 540; sleep 0.2     # View, then into its submenu
    ptr click 1000 540; sleep 0.2                                 # a View choice
    ptr rclick 900 500; sleep 0.2; ptr abs 930 541; sleep 0.2; ptr abs 1000 541; sleep 0.15; escape      # Sort by
    ptr rclick 60 60; sleep 0.2; escape                           # an icon's menu
    ptr click 60 60; sleep 0.1; ptr dclick 60 60; sleep 0.6       # open: starts the file manager
    sleep 0.3; close_windows
  fi
}

phase_ipc_and_layouts() {
  echo "    layouts: keyboard layout, idle inhibit, window queries"
  next_layout
  ipc "LAYOUT_NEXT" >/dev/null
  ipc "LAYOUTS?" >/dev/null
  send_ipc "IDLE_INHIBIT 1"; sleep 0.15; send_ipc "IDLE_INHIBIT 0"
  open_window "${BUILD_DIR}/apps/settings/fleetwm-settings" --page theme
  open_window "${BUILD_DIR}/apps/shortcuts/fleetwm-shortcuts"
  wait_windows 2 3
  ipc "WINDOW_ACTIVATE 1" >/dev/null
  ipc "WINDOW_MINIMIZE 0" >/dev/null
  ipc "WINDOW_TOGGLE 0" >/dev/null
  sleep 0.2
  ipc "WINDOW_CLOSE 1" >/dev/null
  close_windows
}

# A different screen size: the layout, bars and frames are recomputed at 1024x768, the smallest supported screen.
phase_small_screen() {
  local name
  name="$(ipc "OUTPUTS?" | awk '/^OUTPUT /{print $2; exit}')"
  [[ -n "$name" ]] || return 0
  echo "    screen size: ${name} to 1024x768 and back"
  send_ipc "OUTPUT_SET $name 1024 768 60000 0 0"
  sleep 0.4
  open_window "${BUILD_DIR}/apps/settings/fleetwm-settings" --page display
  open_window foot
  wait_windows 2 3
  snap Left; snap Right; maximize_key; sleep 0.15; maximize_key
  have grim && timeout 10 grim "${RUNTIME_DIR}/shot.png" >/dev/null 2>&1
  close_windows
  send_ipc "OUTPUT_SET $name 1280 720 60000 0 0"
  sleep 0.4
}

# One configuration per round, so over a full run every layout, glass and theme combination is drawn: "layout glass theme".
COMBOS=("desktop true dark" "tiling false dark" "desktop false light" "desktop true catppuccin" "tiling true light"
        "desktop true dracula" "desktop false oled_black" "tiling false catppuccin" "tiling true dracula" "desktop true light")
CORNERS=(rounded square rounded rounded square rounded)
FRAMES=(6 0 12 8 6 10)
SIDES=(right right left right left right)
ALIGNS=(center left center right center center)
BAR_LAYOUTS=(full island capsules full capsules island)
POWER_MODES=(normal performance battery_saver performance normal battery_saver)
EDGES=(bottom bottom top left bottom right)   # desktop taskbar position per round (it only matters in desktop rounds; the pointer phases need bottom or top)
SECONDS_ON=(true false true true false true)
DO_WORKSPACES=(1 1 0 0 0 0)
DO_SETTINGS=(1 1 0 1 0 0)
DO_MENUS=(1 1 0 0 0 0)
DO_IPC=(1 0 0 0 0 0)
DO_TILING=(0 1 0 0 1 0)
echo "    running for about ${TRAIN_SECONDS}s: ${#COMBOS[@]} look/layout combinations in turn, each with the full workload..."
# Runs one phase and says how long it took, so a slow step shows up in the training log.
timed() {
  local t0=$SECONDS
  "$@"
  echo "      ($((SECONDS - t0)) s: $1)"
}
while time_left; do
  combo=(${COMBOS[$((round % ${#COMBOS[@]}))]})
  r=$((round % 6))
  round=$((round + 1))
  edge="${EDGES[$r]}"
  # The pointer-driven window phase needs known geometry: the taskbar at the bottom or the top of a 1280x720 screen.
  echo "  round ${round}: ${combo[0]} layout, glass ${combo[1]}, ${combo[2]} theme, corners ${CORNERS[$r]}, frame ${FRAMES[$r]} px, bar ${BAR_LAYOUTS[$r]}, power ${POWER_MODES[$r]}, taskbar ${edge} ($(( END - SECONDS ))s left)"
  set_look "${combo[0]}" "${combo[1]}" "${combo[2]}" false "${CORNERS[$r]}" "${FRAMES[$r]}" "${SIDES[$r]}" "${ALIGNS[$r]}"
  set_bar "${BAR_LAYOUTS[$r]}" "${POWER_MODES[$r]}" "$edge" "${SECONDS_ON[$r]}"
  # What runs in which round. The code a phase reaches does not depend on the look, so the look-independent phases
  # (workspaces, menus, the IPC queries) run in the first rounds only; every round draws the icons; the pointer-driven
  # window and taskbar phases run where the geometry is known (taskbar at the bottom or the top). Three visits of the
  # Settings phase (rounds 1, 2 and 4) show all thirteen pages (five each).
  if (( DO_WORKSPACES[r] )); then timed phase_workspaces; else send_ipc "WORKSPACE 1"; send_ipc "WORKSPACE 2"; send_ipc "WORKSPACE 0"; fi
  time_left && timed phase_icons
  # the file manager and the desktop menus: early, in the first two rounds (one desktop, one tiling), so a slow machine's short run still reaches them
  (( r == 0 || r == 1 )) && time_left && timed phase_files_and_desktop "${combo[0]}"
  if [[ "${combo[0]}" == desktop ]]; then
    if [[ "$edge" == bottom ]]; then
      time_left && timed phase_window_elements "${FRAMES[$r]}" 0
      time_left && timed phase_taskbar_pointer 698
    elif [[ "$edge" == top ]]; then
      time_left && timed phase_window_elements "${FRAMES[$r]}" 44
      time_left && timed phase_taskbar_pointer 22
    fi
  elif (( DO_TILING[r] )); then
    time_left && timed phase_tiling
  fi
  (( DO_SETTINGS[r] )) && time_left && timed phase_windows_and_settings
  (( DO_MENUS[r] )) && time_left && timed phase_menus
  (( DO_IPC[r] )) && time_left && timed phase_ipc_and_layouts
  (( r == 2 )) && time_left && timed phase_small_screen
  # Flip the layout while windows are open, so the relayout and snap paths run with real content.
  if time_left; then
    open_window "${BUILD_DIR}/apps/settings/fleetwm-settings" --page bar
    open_window "${BUILD_DIR}/apps/shortcuts/fleetwm-shortcuts"
    wait_windows 2 3
    if [[ "${combo[0]}" == tiling ]]; then
      set_look desktop "${combo[1]}" "${combo[2]}" false "${CORNERS[$r]}" "${FRAMES[$r]}" "${SIDES[$r]}" "${ALIGNS[$r]}"
    else
      set_look tiling "${combo[1]}" "${combo[2]}" false "${CORNERS[$r]}" "${FRAMES[$r]}" "${SIDES[$r]}" "${ALIGNS[$r]}"
    fi
    sleep 0.3; close_windows
  fi
done
(( round > 0 )) || echo "    the compositor stopped early (see ${COMP_LOG})" >&2

echo "    final workspace switches..."
send_ipc "WORKSPACE 1"
send_ipc "WORKSPACE 0"

echo "    training pass complete after ${round} rounds, shutting everything down cleanly..."
# cleanup() (the EXIT trap) does the actual SIGTERM + wait for every
# child, compositor last.
