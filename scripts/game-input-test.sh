#!/usr/bin/env bash
# Checks the compositor's game support in a private headless session, with no real input devices: builds two tiny test clients and the virtual pointer, then
#   * pointer lock: the client locks the pointer on entering its window; relative moves must arrive as relative-pointer events (REL) and
#     absolute ones must be ignored while locked (no ABS after LOCKED);
#   * fullscreen fit: a client that keeps a 640x480 picture in a fullscreen window is drawn aspect-correct (screenshot in $OUT/fit-*.png, one per mode).
#
#   scripts/game-input-test.sh BUILD_DIR [OUT_DIR]       needs wayland-scanner, cc, grim, dbus-run-session, wayland-protocols
set -uo pipefail
B=$(realpath "$1"); OUT=$(realpath -m "${2:-./game-test-out}"); mkdir -p "$OUT"
SRC=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
T=$(mktemp -d /tmp/fleetwm-gametest.XXXXXX); WP=/usr/share/wayland-protocols
scan() { wayland-scanner client-header "$1" "$T/$2-client-protocol.h"; wayland-scanner private-code "$1" "$T/$2-protocol.c"; }
scan $WP/stable/xdg-shell/xdg-shell.xml xdg-shell
scan $WP/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml pointer-constraints-unstable-v1
scan $WP/unstable/relative-pointer/relative-pointer-unstable-v1.xml relative-pointer-unstable-v1
scan "$SRC/scripts/wlr-virtual-pointer-unstable-v1.xml" wlr-virtual-pointer-unstable-v1
CF=$(pkg-config --cflags --libs wayland-client)
mkdir -p "$T/fakebin"   # a stand-in tuxthrottlectl that only writes down what it is asked
printf '#!/bin/sh\necho "$@" >> "%s/tux.log"\n' "$OUT" > "$T/fakebin/tuxthrottlectl"; chmod +x "$T/fakebin/tuxthrottlectl"
cc -O1 -I"$T" "$SRC/perflog/2026-10-27/game-clients/fit.c" "$T/xdg-shell-protocol.c" -o "$T/fit" $CF || exit 1
cc -O1 -I"$T" "$SRC/perflog/2026-10-27/game-clients/lock.c" "$T/xdg-shell-protocol.c" "$T/pointer-constraints-unstable-v1-protocol.c" "$T/relative-pointer-unstable-v1-protocol.c" -o "$T/lock" $CF || exit 1
cc -O1 -I"$T" "$SRC/scripts/pgo-pointer.c" "$T/wlr-virtual-pointer-unstable-v1-protocol.c" -o "$T/pointer" $CF || exit 1

session() {  # session SCRIPT  -- runs SCRIPT inside a private headless compositor; $RT, $HM and the programs are set
  local RT; RT=$(mktemp -d /tmp/fleetwm-gt.XXXXXX); chmod 700 "$RT"; mkdir -p "$RT/home/.config/fleetwm"
  printf 'window_layout = "desktop"\n' > "$RT/home/.config/fleetwm/theme.toml"
  [[ -n "${CFG:-}" ]] && printf '%s\n' "$CFG" > "$RT/home/.config/fleetwm/game.toml"
  HM=$RT/home
  # a private GameMode config: the test must never run the machine's own start/end scripts (/etc/gamemode.ini)
  printf '[general]\nreaper_freq=5\n\n[custom]\nstart=/bin/true\nend=/bin/true\n' > "$HM/.config/gamemode.ini"
  [[ -n "${PRE:-}" ]] && eval "$PRE"
  cat > "$RT/inner.sh" <<IN
export ${NOBOOST:+FLEETWM_UNUSED=1} ${NOBOOST:-FLEETWM_GAMEMODE_TEST=1} XDG_RUNTIME_DIR=$RT HOME=$RT/home WLR_BACKENDS=headless WLR_RENDERER=pixman LANG=C.UTF-8 NO_AT_BRIDGE=1 PATH=$T/fakebin:$B/src/bar:$B/src/wallpaper:\$PATH
$B/src/compositor/fleetwm > $RT/comp.log 2>&1 &
CP=\$!
for i in \$(seq 1 100); do [ -S $RT/wayland-0 ] && break; sleep 0.1; done
export WAYLAND_DISPLAY=wayland-0 T=$T OUT=$OUT RT=$RT
$1
kill -TERM \$CP 2>/dev/null; sleep 0.5
IN
  dbus-run-session -- bash "$RT/inner.sh" >/dev/null 2>&1
  grep -i "assert" "$RT/comp.log" | head -2
  rm -rf "$RT"
}

echo "== pointer lock"
LOG=$OUT/lock.log; : > "$LOG"
session '
$T/lock > '"$LOG"' 2>&1 & LP=$!
sleep 1.5
for y in 150 250 350 450; do for x in 150 300 450 600 750 900 1050; do $T/pointer abs $x $y; if grep -q LOCKED '"$LOG"'; then break 2; fi; done; done
sleep 0.5
$T/pointer rel 7 3 rel -4 9 abs 100 100 rel 2 2 wait 300
kill $LP
'
cat "$LOG" | tr '\n' ' '; echo
rel=$(grep -c '^REL' "$LOG"); abs_after=$(awk '/LOCKED/{f=1;next} f&&/^ABS/{n++} END{print n+0}' "$LOG")
if grep -q LOCKED "$LOG" && [[ ${rel} -ge 3 && ${abs_after} -eq 0 ]]; then echo "PASS: locked, ${rel} relative events, no absolute motion after the lock"; else echo "FAIL: locked=$(grep -c LOCKED "$LOG") rel=${rel} abs_after_lock=${abs_after}"; fi

for mode in fit integer stretch off; do
  echo "== fullscreen $mode"
  CFG="fullscreen_scaling = \"$mode\"" session '
$T/fit & FP=$!
sleep 3
grim $OUT/fit-'"$mode"'.png
kill $FP
'
done
echo "== gamemode (a windowed steam_app_ program must be registered, and released when it closes)"
GM=$OUT/gamemode.log; : > "$GM"; : > "$OUT/tux.log"
session '
gamemoded > '"$OUT"'/gamemoded.log 2>&1 & GP=$!
sleep 1
FIT_APP_ID=steam_app_999 $T/fit & FP=$!
sleep 3
echo "during: $(gamemoded -s 2>&1)" >> '"$GM"'
kill $FP
sleep 1.5
echo "after: $(gamemoded -s 2>&1)" >> '"$GM"'
kill $GP
'
cat "$GM"
if [[ "$(grep '^gamemode' "$OUT/tux.log" | tr '\n' ' ')" == "gamemode on gamemode off " ]]; then echo "PASS: TuxThrottle Game Mode was switched on with the game and off after it"; else echo "FAIL: tuxthrottlectl calls: $(tr '\n' '|' < "$OUT/tux.log")"; fi
if grep -q "^during: gamemode is active" "$GM" && grep -q "^after: gamemode is inactive" "$GM"; then echo "PASS: gamemoded saw the steam_app_ window and let go of it when it closed"; else echo "FAIL (gamemoded log follows)"; head -8 "$OUT/gamemoded.log"; fi
echo "== startup apps (an XDG autostart entry must be run at session start, a Hidden one must not)"
MK=$OUT/autostart-marker; rm -f "$MK" "$MK.hidden"
PRE='mkdir -p $HM/.config/autostart
printf "[Desktop Entry]\nType=Application\nName=A\nExec=touch '"$MK"' %%U\n" > $HM/.config/autostart/a.desktop
printf "[Desktop Entry]\nType=Application\nName=B\nExec=touch '"$MK"'.hidden\nHidden=true\n" > $HM/.config/autostart/b.desktop' session 'sleep 3'
if [[ -e "$MK" && ! -e "$MK.hidden" ]]; then echo "PASS: the enabled startup app ran, the hidden one did not"; else echo "FAIL: ran=$([[ -e $MK ]] && echo yes || echo no) hidden_ran=$([[ -e $MK.hidden ]] && echo yes || echo no)"; fi
echo "== real-time priority while a game runs (rtkit), normal again afterwards"
RTL=$OUT/realtime.log; : > "$RTL"
CFG='realtime = true' session '
FIT_APP_ID=steam_app_999 $T/fit & FP=$!
sleep 3
echo "during: $(chrt -p $CP 2>&1 | tail -2 | tr "\n" " ")" >> '"$RTL"'
kill $FP
sleep 1.5
echo "after: $(chrt -p $CP 2>&1 | tail -2 | tr "\n" " ")" >> '"$RTL"'
'
cat "$RTL"
if grep -q "^during:.*SCHED_RR\|^during:.*SCHED_FIFO" "$RTL" && grep -q "^after:.*SCHED_OTHER" "$RTL"; then echo "PASS: the compositor ran real-time during the game and went back to normal"; else echo "NOTE: not real-time (rtkit missing or refused); see the lines above"; fi

echo "== late render start and game mode switching must not disturb a fullscreen game"
CFG='max_render_time_ms = 5
mode_switch = true' session '
$T/fit & FP=$!
sleep 3
grim $OUT/late.png
kill $FP
'
if [[ -s "$OUT/late.png" ]]; then echo "PASS: fullscreen frame delivered with late render start and mode_switch on"; else echo "FAIL: no screenshot"; fi

echo "== a headless session must not boost the machine (no GameMode, no TuxThrottle) unless the test switch is set"
: > "$OUT/tux.log"
NOBOOST=1 session '
FIT_APP_ID=steam_app_999 $T/fit & FP=$!
sleep 3
kill $FP
'
if ! grep -q '^gamemode' "$OUT/tux.log"; then echo "PASS: nothing was switched on by a headless compositor"; else echo "FAIL: tuxthrottlectl was called: $(tr '\n' '|' < "$OUT/tux.log")"; fi
rm -rf "$T"
echo "screenshots in $OUT"
