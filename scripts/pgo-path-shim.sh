#!/usr/bin/env bash
# Makes the freshly built (instrumented) programs the ones that are found by name.
#
#   scripts/pgo-path-shim.sh BUILD_DIR SHIM_DIR
#
# The compositor starts the bar, the wallpaper and the keep-awake padlock by name, and the desktop starts
# the launcher, Settings, the power menu and so on by name too. On a machine that already has Fleetwm
# installed those names resolve to the OLD installed copies, which record nothing for the profile; on a
# fresh machine they resolve to nothing at all. The training run puts SHIM_DIR first on PATH, and this
# fills it with symlinks to the programs in BUILD_DIR, so every program the training starts is the
# instrumented one. The unit tests, the login screen and the locker (they need PAM) are left out.
set -euo pipefail

build="${1:?usage: pgo-path-shim.sh BUILD_DIR SHIM_DIR}"
shim="${2:?usage: pgo-path-shim.sh BUILD_DIR SHIM_DIR}"
mkdir -p "${shim}"

find "${build}" -maxdepth 3 -type f -perm -u+x -name 'fleetwm*' \
  ! -name 'fleetwm-unit-tests' ! -name '*.so*' ! -name '*.a' ! -name '*.o' \
  ! -name 'fleetwm-greet*' ! -name 'fleetwm-locker' -print0 |
while IFS= read -r -d '' exe; do
  ln -sf "$(cd "$(dirname "${exe}")" && pwd)/$(basename "${exe}")" "${shim}/$(basename "${exe}")"
done
