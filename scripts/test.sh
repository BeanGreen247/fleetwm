#!/usr/bin/env bash
# Build and run fleetwm's unit tests -- no compositor, Wayland session or GPU needed.
#
#   scripts/test.sh                  run everything (quiet: one line per suite + summary)
#   scripts/test.sh geometry         run one area (see --areas)
#   scripts/test.sh theme bar        several areas
#   scripts/test.sh -f 'Snap*'       any gtest filter
#   scripts/test.sh -v geometry      verbose: every test case
#   scripts/test.sh -l               list every test case
#   scripts/test.sh --areas          list the area names and what they cover
#   scripts/test.sh -r 20 --shuffle  repeat 20 times in random order (finds order dependence)
#   scripts/test.sh --xml out.xml    also write a JUnit-style report
#   scripts/test.sh --no-build       just run the existing binary
#
# Uses build-test/ next to the sources (override with BUILD_DIR=...). Exit status is
# the test result, so it works in CI and in a pre-commit hook.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "${SCRIPT_DIR}")"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build-test}"

declare -A AREAS=(
  [theme]='ParseHexColor.*:ThemeName.*:ThemeTest.*:TitlebarNames.*:WindowLayoutNames.*:ThemeHome.*'
  [bar]='PowerMode.*:BarLayout.*:TaskbarPosition.*:BarConfigTest.*:BatterySourceReadingTest.*'
  [keybinds]='KeybindsConfigTest.*'
  [geometry]='ResizeEdges.*:ResizedBox.*:Cascade.*:TitlebarLayout.*:TitlebarHit.*:TitleX.*:TaskbarSlots.*:SnapZoneAt.*:SnapBox.*'
  [windows]='WindowList.*'
  [shortcuts]='FormatAltCombo.*:FormatAltShiftCombo.*:ShortcutList.*'
  [ipc]='ExtractJsonStringField.*:IpcSocketPathTest.*:IpcClient.*:IpcClientWithServerTest.*'
  [config]='WallpaperConfigTest.*:DefaultAppsTest.*:OutputConfigTest.*'
  [fleetkit]='DesktopEntryExec.*:FleetkitColor.*:GlassCache.*'
  [power]='PowerActions.*:PowerPolkitRule.*:PowerInstaller.*:PowerIcons.*:PolkitRules.*:PolkitRuleBehaviour.*'
)
declare -A AREA_HELP=(
  [theme]='theme.toml: colors, layout, gaps, titlebar settings'
  [bar]='bar.toml: layout, taskbar position, clock, battery'
  [keybinds]='keybinds.toml'
  [geometry]='window maths: resize edges, snapping, titlebar layout, taskbar slots'
  [windows]='window-list wire format (compositor -> taskbar)'
  [shortcuts]='key labels and the shortcuts list'
  [ipc]='compositor socket client'
  [config]='wallpaper, default apps, outputs'
  [fleetkit]='toolkit helpers: colors, desktop entries'
  [power]='power menu: commands, the polkit rule and its installer step, icon drawing'
)

usage() { sed -n '2,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

filter=""; verbose=0; list=0; repeat=1; shuffle=0; xml=""; build=1; areas=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    -f|--filter) filter="$2"; shift 2 ;;
    -v|--verbose) verbose=1; shift ;;
    -l|--list) list=1; shift ;;
    -r|--repeat) repeat="$2"; shift 2 ;;
    --shuffle) shuffle=1; shift ;;
    --xml) xml="$2"; shift 2 ;;
    --no-build) build=0; shift ;;
    --areas)
      for a in $(printf '%s\n' "${!AREAS[@]}" | sort); do printf '  %-10s %s\n' "$a" "${AREA_HELP[$a]}"; done
      exit 0 ;;
    -*) echo "unknown option: $1 (try --help)" >&2; exit 2 ;;
    *)
      [[ -n "${AREAS[$1]:-}" ]] || { echo "unknown area '$1' (try --areas)" >&2; exit 2; }
      areas+=("$1"); shift ;;
  esac
done

# Static check first (fast): two files of one directory defining the same file-scope name break the
# installer's unity build (see scripts/check-unity-collisions.py).
python3 "${SCRIPT_DIR}/check-unity-collisions.py" "${ROOT}" || exit 1

if (( build )); then
  if [[ ! -f "${BUILD_DIR}/build.ninja" ]]; then
    echo "==> Configuring ${BUILD_DIR}"
    meson setup "${BUILD_DIR}" "${ROOT}" -Dtests=true -Dbuildtype=debugoptimized >/dev/null
  fi
  echo "==> Building the unit tests"
  ninja -C "${BUILD_DIR}" tests/fleetwm-unit-tests 2>&1 | tail -n 3
fi

BIN="${BUILD_DIR}/tests/fleetwm-unit-tests"
[[ -x "${BIN}" ]] || { echo "missing ${BIN} (run without --no-build)" >&2; exit 2; }

if (( list )); then "${BIN}" --gtest_list_tests; exit 0; fi

if [[ -z "${filter}" && ${#areas[@]} -gt 0 ]]; then
  for a in "${areas[@]}"; do filter+="${filter:+:}${AREAS[$a]}"; done
fi

args=()
[[ -n "${filter}" ]] && args+=("--gtest_filter=${filter}")
(( repeat > 1 )) && args+=("--gtest_repeat=${repeat}")
(( shuffle )) && args+=("--gtest_shuffle")
[[ -n "${xml}" ]] && args+=("--gtest_output=xml:${xml}")
(( verbose )) || args+=("--gtest_brief=1")

echo "==> Running${filter:+ (filter: ${filter})}"
exec "${BIN}" "${args[@]}"
