#!/usr/bin/env bash
# Timing and speed figures for install.sh: how long each part took, how fast packages were
# downloaded, how fast the machine compiled, and how fast its disk reads and writes.
# Sourced by install.sh; nothing here changes the system (the disk test writes one
# temporary file in the build folder and deletes it).

STATS_START=$SECONDS
STATS_FETCH_BYTES=0
STATS_FETCH_SECS=0
STATS_PKGS=0
declare -A STATS_PHASE=()
declare -A STATS_PHASE_START=()

stats_phase_start() { STATS_PHASE_START["$1"]=$SECONDS; }
stats_phase_end() { STATS_PHASE["$1"]=$(( SECONDS - ${STATS_PHASE_START["$1"]:-$SECONDS} )); }

# CPU seconds used so far by everything this script has run (compilers, tests, ...).
stats_cpu_seconds() {
  times | sed -n 2p | awk '{
    n = split($1 " " $2, parts, " ")
    total = 0
    for (i = 1; i <= n; i++) { split(parts[i], t, /[ms]/); total += t[1] * 60 + t[2] }
    printf "%.1f", total }'
}
STATS_CPU_START=$(stats_cpu_seconds)

# apt-get install with a live progress line, remembering what was downloaded and how fast
# ("Fetched 14.7 MB in 3s"). Non-interactive on purpose: the output is hidden behind the
# progress line, so a question from a package (a config file prompt) would look like a hang;
# existing config files are kept.
# Every apt call of the installer goes through these, so none of them can wait for an answer nobody gives:
#   * needrestart (a post-install hook on Debian and Ubuntu images) is told to restart nothing and ask nothing;
#   * the dpkg lock is waited for (up to 5 minutes) instead of failing at once, and a stalled mirror times out and retries;
#   * stdin is /dev/null, so a package that tries to ask gets end-of-file instead of a silent wait.
APT_ENV=(DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=a NEEDRESTART_SUSPEND=1 APT_LISTCHANGES_FRONTEND=none)
APT_OPTS=(-o DPkg::Lock::Timeout=300 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30 -o Acquire::Retries=3)
apt_update() { sudo env "${APT_ENV[@]}" apt-get update -qq "${APT_OPTS[@]}" </dev/null; }

apt_install() {
  local out rc fetched bytes secs
  out="$(mktemp)"
  ui_live apt "Installing $*" "${out}" \
    sudo env "${APT_ENV[@]}" apt-get install -y "${APT_OPTS[@]}" \
      -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold "$@" </dev/null && rc=0 || rc=$?
  fetched="$(awk '/^Fetched /' "${out}" | tail -1)"
  if [[ -n "${fetched}" ]]; then
    read -r bytes secs < <(echo "${fetched}" | awk '{
      gsub(",", "", $2); v = $2; u = $3
      m = (u == "kB") ? 1e3 : (u == "MB") ? 1e6 : (u == "GB") ? 1e9 : 1
      s = 0
      for (i = 5; i <= NF; i++) {
        if ($i ~ /^\(/) break
        if ($i ~ /h$/) s += $i * 3600
        else if ($i ~ /min$/) s += $i * 60
        else if ($i ~ /s$/) s += $i
      }
      printf "%.0f %.0f", v * m, s }')
    STATS_FETCH_BYTES=$(( STATS_FETCH_BYTES + ${bytes:-0} ))
    STATS_FETCH_SECS=$(( STATS_FETCH_SECS + ${secs:-0} ))
  fi
  STATS_PKGS=$(( STATS_PKGS + $(grep -c '^Setting up ' "${out}" || true) ))
  rm -f "${out}"
  return "${rc}"
}

stats_fmt_time() {  # seconds -> "1h 02m 03s" / "4m 07s" / "12s"
  local s=$1
  if (( s >= 3600 )); then printf "%dh %02dm %02ds" $((s / 3600)) $((s % 3600 / 60)) $((s % 60))
  elif (( s >= 60 )); then printf "%dm %02ds" $((s / 60)) $((s % 60))
  else printf "%ds" "${s}"; fi
}

stats_fmt_bytes() {  # bytes -> "412.3 MB"
  awk -v b="$1" 'BEGIN { if (b >= 1e9) printf "%.2f GB", b / 1e9; else if (b >= 1e6) printf "%.1f MB", b / 1e6; else printf "%.0f kB", b / 1e3 }'
}

stats_rate() {  # bytes seconds -> "14.2 MB/s (0.014 GB/s)"
  awk -v b="$1" -v s="$2" 'BEGIN { if (s <= 0) { print "n/a"; exit } r = b / s; printf "%.1f MB/s (%.3f GB/s)", r / 1e6, r / 1e9 }'
}

# Sequential write then read of a 256 MiB file in $1: returns "write_bytes_per_s read_bytes_per_s".
stats_disk_speed() {
  local dir="$1" f t0 t1 t2 bytes=268435456
  f="${dir}/.fleetwm-speedtest"
  t0=$(date +%s.%N)
  dd if=/dev/zero of="${f}" bs=1M count=256 conv=fdatasync status=none 2>/dev/null || { echo "0 0"; return; }
  t1=$(date +%s.%N)
  sync
  echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null 2>&1 || true  # read from the disk, not memory
  dd if="${f}" of=/dev/null bs=1M status=none 2>/dev/null || true
  t2=$(date +%s.%N)
  rm -f "${f}"
  awk -v b="${bytes}" -v t0="${t0}" -v t1="${t1}" -v t2="${t2}" 'BEGIN { printf "%.0f %.0f", b / (t1 - t0), b / (t2 - t1) }'
}

# Prints the summary. $1 = source dir, $2 = build dir.
stats_summary() {
  local src="$1" build="$2" total=$(( SECONDS - STATS_START ))
  local cpu_end cpu cores lines build_secs train_secs s1 s3 tests_line
  cpu_end=$(stats_cpu_seconds)
  cpu=$(awk -v a="${STATS_CPU_START}" -v b="${cpu_end}" 'BEGIN { printf "%.0f", b - a }')
  cores=$(nproc 2>/dev/null || echo 1)
  s1=$(awk '$1 == "stage1" { print $2 }' "${build}.times" 2>/dev/null)
  train_secs=$(awk '$1 == "train" { print $2 }' "${build}.times" 2>/dev/null)
  s3=$(awk '$1 == "stage3" { print $2 }' "${build}.times" 2>/dev/null)
  build_secs=$(( ${s1:-0} + ${s3:-0} ))
  lines=$(cd "${src}" && git ls-files 'src/*.cpp' 'src/*.hpp' 'src/*.c' 'src/*.h' 2>/dev/null | xargs cat 2>/dev/null | wc -l)
  tests_line=$(grep -h "tests from" "${build}.tests" 2>/dev/null | tail -1 | sed 's/^\[=*\] *//')

  local disk w r
  disk=$(stats_disk_speed "${src}")
  w=${disk% *}; r=${disk#* }

  echo
  echo "==> Install summary"
  printf "  %-22s %s\n" "Total time" "$(stats_fmt_time "${total}")"
  [[ -n "${STATS_PHASE[deps]:-}" ]] && printf "    %-20s %s   (%s packages set up)\n" "Dependencies" "$(stats_fmt_time "${STATS_PHASE[deps]}")" "${STATS_PKGS}"
  [[ -n "${s1}" ]] && printf "    %-20s %s\n" "Instrumented build" "$(stats_fmt_time "${s1}")"
  [[ -n "${train_secs}" ]] && printf "    %-20s %s\n" "Training run" "$(stats_fmt_time "${train_secs}")"
  [[ -n "${s3}" ]] && printf "    %-20s %s\n" "Optimized build" "$(stats_fmt_time "${s3}")"
  [[ -n "${STATS_PHASE[install]:-}" ]] && printf "    %-20s %s\n" "Install and setup" "$(stats_fmt_time "${STATS_PHASE[install]}")"
  [[ -n "${tests_line}" ]] && printf "  %-22s %s\n" "Unit tests" "${tests_line}"
  if (( STATS_FETCH_BYTES > 0 )); then
    printf "  %-22s %s in %s  ->  %s\n" "Downloaded" "$(stats_fmt_bytes "${STATS_FETCH_BYTES}")" \
      "$(stats_fmt_time "${STATS_FETCH_SECS}")" "$(stats_rate "${STATS_FETCH_BYTES}" "${STATS_FETCH_SECS}")"
  else
    printf "  %-22s %s\n" "Downloaded" "nothing (all packages were already installed)"
  fi
  if (( build_secs > 0 )); then
    printf "  %-22s %s source lines, built twice, %s lines/s\n" "Compile" "${lines}" \
      "$(awk -v l="${lines}" -v s="${build_secs}" 'BEGIN { printf "%.0f", 2 * l / s }')"
    # CPU seconds the build used divided by the seconds it took: 2.0x means two cores were busy all the time.
    printf "  %-22s %ss of CPU in %ss = %sx speedup on %s cores\n" "Parallel speedup" "${STATS_BUILD_CPU:-0}" "${build_secs}" \
      "$(awk -v c="${STATS_BUILD_CPU:-0}" -v t="${build_secs}" 'BEGIN { printf "%.2f", (t > 0 ? c / t : 0) }')" "${cores}"
  fi
  printf "  %-22s %s\n" "Disk write" "$(stats_rate "${w:-0}" 1)"
  printf "  %-22s %s\n" "Disk read" "$(stats_rate "${r:-0}" 1)"
  local binbytes=0 f
  for f in "${build}"/src/*/fleetwm*; do [[ -f "${f}" && -x "${f}" ]] && binbytes=$(( binbytes + $(stat -c %s "${f}") )); done
  printf "  %-22s %s of programs\n" "Installed" "$(stats_fmt_bytes "${binbytes}")"
  echo
}
