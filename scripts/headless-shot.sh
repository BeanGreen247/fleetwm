#!/usr/bin/env bash
# Runs one Fleetwm program inside a private headless compositor (own runtime directory and HOME, pixman renderer) and saves a screenshot of the
# headless output with grim. The live session, its screen and its settings are never touched; the only kills are of the pids this script started.
#
#   scripts/headless-shot.sh BUILD_DIR OUT.png PROGRAM [ARGS...]
#   SHOT_WAIT=SECONDS   how long to wait before the screenshot (default 2.5; heavy pages need 4-5)
#   SHOT_CFG='shell'    run inside the setup, with $HM = the private home: write theme.toml, icon theme settings, state files...
#                       e.g. SHOT_CFG='mkdir -p $HM/.config/gtk-3.0; printf "[Settings]\ngtk-icon-theme-name=Papirus-Dark\n" > $HM/.config/gtk-3.0/settings.ini'
# Needs: grim, dbus-run-session, a build with src/compositor/fleetwm, src/bar/fleetwm-bar and src/wallpaper/fleetwm-wallpaper.
set -uo pipefail
B=$(realpath "$1"); OUT=$(realpath -m "$2"); shift 2
RT=$(mktemp -d /tmp/fleetwm-shot.XXXXXX); chmod 700 "$RT"; HM=$RT/home; mkdir -p "$HM/.config/fleetwm"
printf 'window_layout = "desktop"\n' > "$HM/.config/fleetwm/theme.toml"
[[ -n "${SHOT_CFG:-}" ]] && eval "$SHOT_CFG"
cat > "$RT/inner.sh" <<IN
export XDG_RUNTIME_DIR=$RT HOME=$HM WLR_BACKENDS=headless WLR_RENDERER=pixman LANG=C.UTF-8 LC_ALL=C.UTF-8 NO_AT_BRIDGE=1
export PATH=$B/src/bar:$B/src/wallpaper:\$PATH
$B/src/compositor/fleetwm > $RT/comp.log 2>&1 &
echo \$! > $RT/comp.pid
for i in \$(seq 1 100); do [ -S $RT/wayland-0 ] && break; sleep 0.1; done
export WAYLAND_DISPLAY=wayland-0
"\$@" > $RT/prog.log 2>&1 &
echo \$! > $RT/prog.pid
sleep ${SHOT_WAIT:-2.5}
grim "$OUT"
kill -0 \$(cat $RT/prog.pid) 2>/dev/null || echo "the program had already exited" >&2
kill -TERM \$(cat $RT/prog.pid) 2>/dev/null; sleep 0.3
kill -TERM \$(cat $RT/comp.pid) 2>/dev/null; sleep 0.5
IN
dbus-run-session -- bash "$RT/inner.sh" "$@" >/dev/null 2>&1
tail -3 "$RT/prog.log" >&2
rm -rf "$RT"
