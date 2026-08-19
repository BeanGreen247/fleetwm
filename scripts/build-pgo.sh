#!/usr/bin/env bash
# Two-stage profile-guided optimization (PGO) build. install.sh now runs
# both stages unconditionally via build-pgo-auto.sh (which wraps this
# script's `generate`/`use` pair around a synthetic training pass) --
# every fresh install is PGO-built, not just ones someone happened to
# train by hand. This script also still works standalone for a real
# live-usage training session instead of the synthetic one, if you want
# a profile shaped by how you actually use fleetwm day to day rather
# than the synthetic pass's fixed script:
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

# Same release/LTO/native-arch/gc-sections/G_DISABLE_ASSERT/bind-now
# flags as install.sh (see its own comments for why), plus -Db_pgo --
# strip only on the final "use" build (no reason to strip an
# instrumented throwaway binary you're about to rebuild anyway).
EXTRA_OPTS=()
if [[ "$MODE" == "use" ]]; then
  EXTRA_OPTS+=(-Dstrip=true)
fi

meson setup "${BUILD_DIR}" "${SCRIPT_DIR}" --prefix=/usr/local --buildtype=release \
  -Db_ndebug=true -Db_lto=true -Db_pgo="${MODE}" -Dtests=true \
  -Dc_args='-march=native -ffunction-sections -fdata-sections -fno-semantic-interposition -DG_DISABLE_ASSERT' \
  -Dcpp_args='-march=native -ffunction-sections -fdata-sections -fno-semantic-interposition -DG_DISABLE_ASSERT' \
  -Dc_link_args='-Wl,--gc-sections -Wl,-z,now' -Dcpp_link_args='-Wl,--gc-sections -Wl,-z,now' \
  "${EXTRA_OPTS[@]}" --reconfigure

ninja -C "${BUILD_DIR}"

echo "==> Running unit tests"
# Run the gtest binary directly rather than `meson test` -- meson treats
# the whole binary as a single test ("1/1 fleetwm-unit-tests OK"), which
# hides the real per-case count/results. Running it directly prints every
# individual RUN/OK line plus gtest's own summary ("N tests from M test
# suites ran ... PASSED N tests"), and still exits non-zero on any
# failure, so `set -euo pipefail` above still aborts the install exactly
# as before.
"${BUILD_DIR}/tests/fleetwm-unit-tests"

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
