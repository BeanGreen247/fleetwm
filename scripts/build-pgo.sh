#!/usr/bin/env bash
# Two-stage profile-guided optimization (PGO) build -- opt-in and
# deliberately separate from install.sh's normal single-pass flow. PGO
# needs a real usage sample collected *between* two builds (open/close
# apps, switch workspaces, use the launcher, tile/resize windows, use
# the bar for a while), which doesn't fit a scripted "just run it"
# install step without either slowing down every fresh install or
# risking a profile from a training run too short to actually help.
#
# Usage:
#   scripts/build-pgo.sh generate
#     -> builds instrumented binaries in build-pgo/. Install them
#        (sudo ninja -C build-pgo install), then use fleetwm normally
#        for 10-15+ minutes.
#
#   IMPORTANT: quit the instrumented compositor via its own Alt+Escape
#   keybind (a clean wl_display_terminate() -> normal exit()), NOT
#   `kill`/`pkill` from outside. GCC's profiling runtime flushes
#   collected .gcda data via an atexit handler that only runs on a
#   normal exit() -- a bare SIGTERM/SIGKILL skips that entirely and the
#   whole training session's data is silently lost. Same goes for every
#   other fleetwm process you want profiled (bar, settings, launcher,
#   wallpaper): let them exit normally rather than pkilling them.
#
#   scripts/build-pgo.sh use
#     -> rebuilds build-pgo/ using the .gcda profile data collected
#        above, producing the final PGO-optimized binaries. Both stages
#        MUST reuse the same build directory: GCC's -fprofile-generate
#        embeds paths (relative to where each object file was compiled)
#        for where to later look for -fprofile-use's .gcda files: a
#        fresh/different build directory between stages means "use"
#        finds no profile data and GCC just warns and falls back to an
#        unprofiled build with no error to notice.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-pgo"

MODE="${1:-}"
if [[ "$MODE" != "generate" && "$MODE" != "use" ]]; then
  echo "Usage: $0 {generate|use}" >&2
  exit 1
fi

# Same release/LTO/native-arch/gc-sections flags as install.sh, plus
# -Db_pgo -- strip only on the final "use" build (no reason to strip an
# instrumented throwaway binary you're about to rebuild anyway).
EXTRA_OPTS=()
if [[ "$MODE" == "use" ]]; then
  EXTRA_OPTS+=(-Dstrip=true)
fi

meson setup "${BUILD_DIR}" "${SCRIPT_DIR}" --prefix=/usr/local --buildtype=release \
  -Db_ndebug=true -Db_lto=true -Db_pgo="${MODE}" \
  -Dc_args='-march=native -ffunction-sections -fdata-sections -fno-semantic-interposition' \
  -Dcpp_args='-march=native -ffunction-sections -fdata-sections -fno-semantic-interposition' \
  -Dc_link_args=-Wl,--gc-sections -Dcpp_link_args=-Wl,--gc-sections \
  "${EXTRA_OPTS[@]}" --reconfigure

ninja -C "${BUILD_DIR}"

echo
if [[ "$MODE" == "generate" ]]; then
  echo "==> Instrumented build ready in ${BUILD_DIR}."
  echo "    sudo ninja -C ${BUILD_DIR} install"
  echo
  echo "    Then use fleetwm normally for 10-15+ minutes: open/close apps,"
  echo "    switch workspaces, use the launcher, tile/resize windows, use"
  echo "    the bar. The more representative this session, the better the"
  echo "    final optimization."
  echo
  echo "    Quit via Alt+Escape when done (NOT kill/pkill -- see this"
  echo "    script's header comment for why), then run:"
  echo "        $0 use"
else
  echo "==> PGO-optimized build ready in ${BUILD_DIR}."
  echo "    sudo ninja -C ${BUILD_DIR} install    # to actually install it"
fi
