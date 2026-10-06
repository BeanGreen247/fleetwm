#!/usr/bin/env bash
# Fully automatic profile-guided optimization (PGO) build -- no live
# 10-15 minute human usage session required, unlike scripts/build-pgo.sh
# (generate/use) which this wraps. install.sh calls this unconditionally
# for every install (not opt-in) -- see its own "Building with PGO" step.
# Instead, a synthetic training pass (scripts/pgo-train-session.sh)
# drives the instrumented binaries itself: boots the compositor under
# wlroots' headless backend (no real DRM/seat needed) inside an isolated
# D-Bus session bus and scratch XDG_RUNTIME_DIR, exercises it over its
# own IPC socket (workspace switches), and runs the bar/wallpaper/
# settings/launcher/powermenu/audiomixer programs alongside it for a
# fixed window, then quits everything
# cleanly via SIGTERM -- safe because every fleetwm client now installs
# fleetwm::install_clean_quit() (src/common/clean_quit.cpp), which
# lets gcov's atexit-based .gcda flush actually run instead of being
# skipped by an abrupt kill.
#
# Coverage is narrower than a real human session: locker (needs real
# PAM auth to reach its own clean-unlock exit) and the greeter binaries
# (TTY/PAM-driven, not Wayland clients like the rest) aren't trained here. Still a
# real, repeatable improvement over training only the compositor, which
# is what every PGO build before this one did.
#
# Usage: scripts/build-pgo-auto.sh [training-seconds, default 150]
#
# Training runs the freshly built binaries straight out of build-pgo/,
# not an installed copy, so no sudo is needed until the very end, when
# you decide to actually install the finished PGO-optimized build.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-pgo"
TRAIN_SECONDS="${1:-150}"

for tool in dbus-run-session python3; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "error: $tool is required but not found on PATH" >&2
    exit 1
  fi
done

export FLEETWM_PGO_AUTO=1
TIMES="${BUILD_DIR}.times"
: > "${TIMES}"
T=$SECONDS
echo "==> Stage 1/3: instrumented build: a first compile that counts how often each part of the code runs"
bash "${SCRIPT_DIR}/scripts/build-pgo.sh" generate

echo "stage1 $(( SECONDS - T ))" >> "${TIMES}"
T=$SECONDS
echo "==> Stage 2/3: training run (${TRAIN_SECONDS}s): the instrumented desktop runs on a virtual screen while it records which code is used"
RUNTIME_DIR="$(mktemp -d /tmp/fleetwm-pgo-train.XXXXXX)"
chmod 700 "$RUNTIME_DIR"
cleanup_runtime_dir() {
  rm -rf "$RUNTIME_DIR"
}
trap cleanup_runtime_dir EXIT

# shellcheck source=scripts/install-ui.sh
source "${SCRIPT_DIR}/scripts/install-ui.sh"
ui_live plain "Training" "${BUILD_DIR}.train.log" dbus-run-session -- env \
  WLR_BACKENDS=headless WLR_RENDERER=pixman \
  XDG_RUNTIME_DIR="$RUNTIME_DIR" HOME="$RUNTIME_DIR" \
  LANG=C.UTF-8 LC_ALL=C.UTF-8 \
  bash "${SCRIPT_DIR}/scripts/pgo-train-session.sh" "$RUNTIME_DIR" "$TRAIN_SECONDS" "$BUILD_DIR"

echo "train $(( SECONDS - T ))" >> "${TIMES}"
T=$SECONDS
echo "==> Stage 3/3: final build: compiled again, now optimized with what the training run recorded"
bash "${SCRIPT_DIR}/scripts/build-pgo.sh" use
echo "stage3 $(( SECONDS - T ))" >> "${TIMES}"

echo
echo "==> Done. Install the final optimized build with:"
echo "    sudo ninja -C ${BUILD_DIR} install"
