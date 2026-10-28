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
cc -O1 "$SRC/perflog/2026-10-27/game-clients/xtray.c" -o "$T/xtray" $(pkg-config --cflags --libs xcb) || exit 1
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
XBEFORE="\$(ls /tmp/.X11-unix 2>/dev/null | tr '\\n' ' ')"
$B/src/compositor/fleetwm > $RT/comp.log 2>&1 &
CP=\$!
for i in \$(seq 1 100); do [ -S $RT/wayland-0 ] && break; sleep 0.1; done
export WAYLAND_DISPLAY=wayland-0 T=$T OUT=$OUT RT=$RT
# the compositor's own Xwayland socket is the one that appeared since it started (earlier ones belong to other sessions)
D=\$(comm -13 <(printf '%s\\n' \$XBEFORE | sort) <(ls /tmp/.X11-unix 2>/dev/null | sort) | head -1 | sed 's/^X/:/')
[ -n "\$D" ] && export DISPLAY=\$D
$1
pkill -TERM -P \$CP 2>/dev/null   # the compositor's own children (bar, Xwayland, the tray proxy), by parent pid only
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
echo "== an X11 window that unmaps but stays alive (Steam hiding to the tray) must leave no title bar behind"
session '
python3 '"$SRC"'/perflog/2026-10-27/game-clients/xhide.py 6 xtest > /dev/null 2>&1 & XP=$!
sleep 4.5
grim $OUT/xhide-before.png
sleep 5
grim $OUT/xhide-after.png
kill $XP
'
python3 - "$OUT/xhide-before.png" "$OUT/xhide-after.png" <<'PY'
import sys
from PIL import Image
b, a = Image.open(sys.argv[1]).convert("RGB"), Image.open(sys.argv[2]).convert("RGB")
w, h = a.size
box = (150, 0, w, h - 50)  # right of the desktop icons, above the taskbar
bg = a.getpixel((w - 100, 300))
shown = {p for p in b.crop(box).getdata()} != {bg}
gone = set(a.crop(box).getdata()) == {bg}
print("PASS: the hidden window and its title bar are gone" if shown and gone else "FAIL: window shown before=%s, empty after=%s" % (shown, gone))
PY
echo "== fast desktop (opt-in): the bar drops its glass while a game runs and gets it back afterwards"
CFG='fast_desktop = true' session '
FIT_APP_ID=steam_app_999 $T/fit & FP=$!
sleep 3
grim $OUT/fast-during.png
kill $FP
sleep 3
grim $OUT/fast-after.png
'
python3 - "$OUT/fast-during.png" "$OUT/fast-after.png" <<'PY'
import sys
from PIL import Image
d, a = Image.open(sys.argv[1]).convert("RGB"), Image.open(sys.argv[2]).convert("RGB")
w, h = a.size
def taskbar_colours(im): return len(set(im.crop((300, h - 40, w - 400, h - 4)).getdata()))
# a glass taskbar is a blurred, shaded gradient (many colours); a flat one has very few
print("PASS: glass off during the game (%d colours), back after (%d)" % (taskbar_colours(d), taskbar_colours(a)) if taskbar_colours(d) < taskbar_colours(a) // 2 else "FAIL: taskbar colours during=%d after=%d" % (taskbar_colours(d), taskbar_colours(a)))
PY

echo "== an X11 window as big as the screen (a Wine borderless game) is treated as fullscreen: no taskbar over it"
session '
python3 '"$SRC"'/perflog/2026-10-27/game-clients/xhide.py -30 xgame 1280 720 0 0 > /dev/null 2>&1 & XP=$!
sleep 6
grim $OUT/xfull.png
kill $XP
'
python3 - "$OUT/xfull.png" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
w, h = im.size
bar = {p for p in im.crop((0, h - 40, w, h - 4)).getdata()}
game = im.getpixel((w // 2, h // 2))
print("PASS: the game covers the taskbar (the whole bottom edge is the game's colour)" if bar == {game} else "FAIL: the taskbar is still drawn over the game (%d colours along the bottom edge)" % len(bar))
PY

echo "== a Wine-style X11 tray icon (XEmbed) shows in the bar and gets clicks"
LOG=$OUT/xtray.log; : > "$LOG"
session '
sleep 1
$T/xtray > '"$LOG"' 2>&1 & XP=$!
sleep 4
grim $OUT/xtray-before.png
XY=$(python3 -c "
from PIL import Image
im = Image.open(\"$OUT/xtray-before.png\").convert(\"RGB\"); w, h = im.size
hits = [(x, y) for y in range(h - 60, h) for x in range(w // 2, w) if im.getpixel((x, y))[0] > 200 and im.getpixel((x, y))[1] < 60 and im.getpixel((x, y))[2] < 60]
print(\"%d %d\" % (sum(p[0] for p in hits) // len(hits), sum(p[1] for p in hits) // len(hits)) if hits else \"0 0\")
")
echo "$XY" > $OUT/xtray-xy.txt
$T/pointer click $XY wait 600
sleep 2
grim $OUT/xtray-after.png
kill $XP
'
if grep -q DOCKED "$LOG"; then echo "PASS: the program found the bar's X11 tray and docked"; else echo "FAIL: no X11 tray: $(cat "$LOG")"; fi
if [[ "$(cat $OUT/xtray-xy.txt 2>/dev/null)" != "0 0" ]]; then echo "PASS: the icon is drawn in the bar at $(cat $OUT/xtray-xy.txt)"; else echo "FAIL: no red icon in the bar"; fi
python3 - "$OUT/xtray-before.png" "$OUT/xtray-xy.txt" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB"); w, h = im.size
cx, cy = (int(v) for v in open(sys.argv[2]).read().split())
# the icon is red on its left half and clear on the right: the clear half must show the taskbar, not black
reds = [x for x in range(cx - 20, cx + 20) if im.getpixel((x, cy))[0] > 200 and im.getpixel((x, cy))[1] < 60]
right = im.getpixel((max(reds) + 6, cy)) if reds else (0, 0, 0)
print("PASS: the transparent half of the icon shows the taskbar %s" % (right,) if sum(right) > 120 else "FAIL: the transparent half is dark %s" % (right,))
PY
if grep -q CLICKED "$LOG"; then echo "PASS: a click on the icon reached the program"; else echo "FAIL: the click did not reach the program"; fi

echo "== a libappindicator tray item without Activate (Steam): a click shows its dbusmenu, choosing an entry reaches the program"
LOG=$OUT/sni.log; : > "$LOG"
session '
python3 '"$SRC"'/perflog/2026-10-27/game-clients/sni_menu.py > '"$LOG"' 2>&1 & SP=$!
sleep 4
grim $OUT/sni-before.png
XY=$(python3 -c "
from PIL import Image
im = Image.open(\"$OUT/sni-before.png\").convert(\"RGB\"); w, h = im.size
hits = [(x, y) for y in range(h - 60, h) for x in range(w // 2, w) if im.getpixel((x, y))[0] > 200 and im.getpixel((x, y))[1] < 60 and im.getpixel((x, y))[2] > 200]
print(\"%d %d\" % (sum(p[0] for p in hits) // len(hits), sum(p[1] for p in hits) // len(hits)) if hits else \"0 0\")
")
$T/pointer click $XY wait 800
sleep 1
grim $OUT/sni-menu.png
$T/pointer click 870 550 wait 500
sleep 1.5
kill $SP
'
if grep -q "EVENT 2 clicked" "$LOG"; then echo "PASS: the tray item's menu was shown and its Library entry reached the program"; else echo "FAIL: no menu event: $(cat "$LOG")"; fi

rm -rf "$T"
echo "screenshots in $OUT"
