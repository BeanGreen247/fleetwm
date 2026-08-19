#!/usr/bin/env bash
# Convenience wrapper: build (or reconfigure) a throwaway build
# directory with the unit-test suite enabled and run it, without
# touching your real `build/` directory or requiring a full
# install.sh run. Useful for quickly checking a change to
# src/common/ compiles and passes before doing anything heavier.
#
# Usage: scripts/run-tests.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-tests"

meson setup "${BUILD_DIR}" "${SCRIPT_DIR}" -Dtests=true --reconfigure
ninja -C "${BUILD_DIR}"
# Run the gtest binary directly rather than `meson test` -- meson treats
# the whole binary as a single test ("1/1 fleetwm-unit-tests OK"), which
# hides the real per-case count/results. Running it directly prints every
# individual RUN/OK line plus gtest's own summary, and still exits
# non-zero on any failure (set -euo pipefail above still aborts).
"${BUILD_DIR}/tests/fleetwm-unit-tests"
