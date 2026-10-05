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
#
# -Dunity=on: both PGO stages already do a full clean-ish rebuild every
# time (install.sh's own doc comment: "not opt-in anymore"), so there's
# no incremental-build cost to protect here -- measured ~32% faster
# clean build on fleetwm-dev (65s -> 45s, 2 vCPUs) with unity on, all
# 272 unit tests still passing. Deliberately NOT enabled in
# scripts/run-tests.sh's throwaway build-tests/ dir, which exists
# specifically for fast incremental iteration on a single changed file
# -- unity would force recompiling every merged file in that file's
# unity group on each touch, working against that script's whole point.
EXTRA_OPTS=()
if [[ "$MODE" == "use" ]]; then
  EXTRA_OPTS+=(-Dstrip=true)
fi

# Speed over safety, on purpose: every hardening option is off (stack protector, stack-clash
# protection, control-flow protection, _FORTIFY_SOURCE, full RELRO, PIE) and the compiler
# may assume maths never traps. -w silences compiler warnings and notes.
COMMON_FLAGS='-w -march=native -ffunction-sections -fdata-sections -fno-semantic-interposition -fno-plt
  -fno-math-errno -fno-trapping-math -fomit-frame-pointer -fno-stack-protector -fno-stack-clash-protection
  -fcf-protection=none -fno-pie -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -DG_DISABLE_ASSERT'
COMMON_FLAGS="$(echo "${COMMON_FLAGS}" | tr '\n' ' ')"
# Our training pass only covers part of the code; without this GCC would optimize everything
# it never saw for size, which can slow real use of the paths it missed.
if [[ "$MODE" == "use" ]]; then
  COMMON_FLAGS+=" -fprofile-partial-training"
fi
LINK_FLAGS='-no-pie -Wl,--gc-sections -Wl,-O1 -Wl,--as-needed -Wl,--sort-common -Wl,-z,lazy -Wl,-z,norelro -Wl,-z,noseparate-code'

# Configure and build quietly: the full output goes to a log, only a progress counter and
# any real error reach the terminal.
LOG="${BUILD_DIR}.log"
: > "${LOG}"
fail() {
  echo
  echo "error: build failed. Last lines of ${LOG}:" >&2
  grep -E "error|Error|FAILED|undefined reference" "${LOG}" | head -20 >&2 || tail -20 "${LOG}" >&2
  exit 1
}
meson setup "${BUILD_DIR}" "${SCRIPT_DIR}" --prefix=/usr/local --buildtype=release \
  -Db_ndebug=true -Db_lto=true -Db_pgo="${MODE}" -Dtests=true -Dunity=on -Dwarning_level=0 -Ddefault_library=static \
  -Dc_args="${COMMON_FLAGS}" -Dcpp_args="${COMMON_FLAGS}" \
  -Dc_link_args="${LINK_FLAGS}" -Dcpp_link_args="${LINK_FLAGS}" \
  "${EXTRA_OPTS[@]}" --reconfigure >> "${LOG}" 2>&1 || fail

echo "==> Compiling (${MODE})"
set +o pipefail
ninja -C "${BUILD_DIR}" 2>&1 | tee -a "${LOG}" | awk '/^\[[0-9]+\/[0-9]+\]/ { printf "\r  %s   ", $1; fflush() } END { print "" }'
status=${PIPESTATUS[0]}
set -o pipefail
[[ "${status}" -eq 0 ]] || fail

echo "==> Running unit tests"
# Run the gtest binary directly rather than `meson test` -- meson treats
# the whole binary as a single test ("1/1 fleetwm-unit-tests OK"), which
# hides the real per-case count/results. Running it directly prints every
# individual RUN/OK line plus gtest's own summary ("N tests from M test
# suites ran ... PASSED N tests"), and still exits non-zero on any
# failure, so `set -euo pipefail` above still aborts the install exactly
# as before.
"${BUILD_DIR}/tests/fleetwm-unit-tests" --gtest_brief=1 2>&1 | tee "${BUILD_DIR}.tests"

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
