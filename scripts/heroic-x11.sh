#!/usr/bin/env bash
# In a Wayland session Heroic Games Launcher (Electron) starts as a native Wayland client; Fleetwm starts it on X11 (Xwayland), where resizing and
# repainting were verified, and keeps a log of every run in ~/.local/state/fleetwm/heroic.log (the previous run in heroic.log.old) with Chromium's own
# logging on and how the run ended (exit code or the signal that killed it), so a crash can be read afterwards.
# Installs ~/.local/bin/fleetwm-heroic and a user copy of Heroic's desktop entry that starts it. A system entry is never edited. Run again to refresh.
set -u
bin="${XDG_BIN_HOME:-$HOME/.local/bin}"
mkdir -p "$bin"
cat > "$bin/fleetwm-heroic" <<'WRAP'
#!/usr/bin/env bash
# Started by Heroic's desktop entry (see scripts/heroic-x11.sh in the fleetwm sources).
exe=""
for c in /usr/lib64/heroic/heroic /usr/lib/heroic/heroic /opt/Heroic/heroic /opt/heroic/heroic; do [[ -x $c ]] && { exe=$c; break; }; done
[[ -n $exe ]] || exe=$(command -v heroic || true)
[[ -n $exe ]] || { echo "fleetwm-heroic: Heroic is not installed" >&2; exit 127; }
dir="${XDG_STATE_HOME:-$HOME/.local/state}/fleetwm"
mkdir -p "$dir"
log="$dir/heroic.log"
[[ -s $log ]] && mv -f "$log" "$log.old"
{
  echo "=== $(date '+%F %T') fleetwm-heroic: $exe $*"
  echo "session=${XDG_SESSION_TYPE:-?} wayland=${WAYLAND_DISPLAY:-?} display=${DISPLAY:-?} gpu_env: DRI_PRIME=${DRI_PRIME:-} NV_PRIME=${__NV_PRIME_RENDER_OFFLOAD:-}"
} > "$log"
# Chromium's own logging, with its GPU, X11/ozone and display code verbose, and (when strace is installed) every system call of Heroic and its helpers, GPU
# ioctls included, written to heroic.log.strace.NNNN in pieces of 200 MB of which the last 6 are kept. FLEETWM_HEROIC_STRACE=0 turns the strace off.
flags=(--ozone-platform=x11 --enable-logging=stderr --log-level=0 --v=0
       "--vmodule=*/gpu/*=2,*/ui/gl/*=2,*/ui/ozone/*=2,*/ui/base/x/*=2,*/ui/gfx/x/*=1,*gpu_*=2,*viz*=1,*window_tree_host*=1" --enable-gpu-service-logging)
rm -f "$log".strace.* 2>/dev/null
started=$(date '+%F %T')
run=("$exe" "${flags[@]}" "$@")
if [[ ${FLEETWM_HEROIC_STRACE:-1} != 0 ]] && command -v strace >/dev/null 2>&1; then
  (while sleep 20; do ls -1 "$log".strace.* 2>/dev/null | head -n -6 | xargs -r rm -f; done) &
  keeper=$!
  ELECTRON_ENABLE_LOGGING=1 ELECTRON_ENABLE_STACK_DUMPING=1 strace -f -tt -T -s 200 -o "|split -b 200M -d -a 4 - $log.strace." "${run[@]}" >> "$log" 2>&1
  rc=$?
  kill "$keeper" 2>/dev/null
else
  echo "(no strace: install it to get a system call log)" >> "$log"
  ELECTRON_ENABLE_LOGGING=1 ELECTRON_ENABLE_STACK_DUMPING=1 "${run[@]}" >> "$log" 2>&1
  rc=$?
fi
if (( rc > 128 )); then how="killed by signal $((rc - 128)) ($(kill -l $((rc - 128)) 2>/dev/null))"; else how="exit code $rc"; fi
echo "=== $(date '+%F %T') Heroic ended: $how" >> "$log"
if (( rc > 128 )) && command -v coredumpctl >/dev/null 2>&1; then
  sleep 3
  { echo "--- coredumpctl"; coredumpctl info --no-pager --since "$started" -1 heroic 2>&1 | head -30
    if command -v gdb >/dev/null 2>&1; then echo "--- backtrace"; timeout 90 coredumpctl debug --since "$started" -1 heroic --debugger-arguments="-batch -ex 'thread apply all bt 12'" 2>&1 | grep -E '^(#|Thread)' | head -150; fi; } >> "$log" 2>&1
fi
exit $rc
WRAP
chmod +x "$bin/fleetwm-heroic"

src=""
for f in /usr/share/applications/com.heroicgameslauncher.hgl.desktop /usr/share/applications/heroic.desktop; do
  [[ -f $f ]] && { src=$f; break; }
done
[[ -n $src ]] || exit 0
dest="${XDG_DATA_HOME:-$HOME/.local/share}/applications/$(basename "$src")"
mkdir -p "$(dirname "$dest")"
sed -E "s|^Exec=[^ ]*heroic( .*)?\$|Exec=$bin/fleetwm-heroic\1|" "$src" > "$dest"
echo "fleetwm: Heroic starts through $bin/fleetwm-heroic, log in ${XDG_STATE_HOME:-$HOME/.local/state}/fleetwm/heroic.log"
