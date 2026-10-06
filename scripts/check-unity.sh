#!/usr/bin/env bash
# Compile the whole tree the way the installer does (unity build, release flags, tests on), without
# LTO or PGO, and report EVERY error instead of stopping at the first. Run it before tagging a release:
# a unity build merges a target's .cpp files into one translation unit, so a file-scope name defined
# in two files compiles on its own and fails here.
#
#   scripts/check-unity.sh            build into build-unity/ next to the sources, then run the unit tests
#   scripts/check-unity.sh --no-run   build only
#   BUILD_DIR=/tmp/ub scripts/check-unity.sh
#
# scripts/check-unity-collisions.py is the quick static version of the same check (it also runs with
# scripts/test.sh and `meson test`).
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "${SCRIPT_DIR}")"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build-unity}"

python3 "${SCRIPT_DIR}/check-unity-collisions.py" "${ROOT}"

if [[ ! -f "${BUILD_DIR}/build.ninja" ]]; then
  echo "==> Configuring ${BUILD_DIR} (unity=on)"
  meson setup "${BUILD_DIR}" "${ROOT}" -Dunity=on -Dtests=true -Dwarning_level=0 \
    --buildtype=release -Db_ndebug=true -Ddefault_library=static >/dev/null
fi
echo "==> Building everything with unity on (all errors are reported)"
log="${BUILD_DIR}/check-unity.log"
if ! ninja -C "${BUILD_DIR}" -k 0 >"${log}" 2>&1; then
  grep -E "error:|FAILED|previously defined|note: .*previously" "${log}" | cut -c1-240 || true
  echo "unity build FAILED (full log: ${log})" >&2
  exit 1
fi
echo "unity build ok"
if [[ "${1:-}" != "--no-run" ]]; then
  "${BUILD_DIR}/tests/fleetwm-unit-tests" --gtest_brief=1
fi
