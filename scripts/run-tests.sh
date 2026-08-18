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
meson test -C "${BUILD_DIR}" --print-errorlogs
