#!/usr/bin/env bash
# Opt-in, runtime-only tuning for a gaming or low-latency session. Nothing is written to /etc: a reboot undoes everything, and `--undo` puts the
# recorded values back now. Every change is printed with its old and new value. `--dry-run` only prints.
#
#   sudo scripts/gaming-tune.sh [--dry-run | --undo]
#
# What it does and why (measure with Hotspot / the compositor's debug overlay before keeping any of it, docs/GAMING.md):
#   CPU governor/EPP -> performance        no ramp-up delay when a frame needs the clock
#   kernel.split_lock_mitigate = 0         old games with split-lock atomics are not throttled (Wine/Proton titles)
#   vm.max_map_count = 2147483642          Proton/DXVK titles that map many regions (the value Steam Deck and Fedora's gaming spins use)
#   transparent_hugepage = madvise        no background compaction stalls; apps that want huge pages still get them
#   NVMe I/O scheduler = none              the drive's own queue is faster than a software scheduler for game loads
#   kernel.sched_autogroup_enabled = 0     a game keeps its share against a many-process desktop; the compositor already runs at nice -10
set -euo pipefail
STATE=/run/fleetwm-gaming-tune.state
mode=apply
case "${1:-}" in --dry-run) mode=dry ;; --undo) mode=undo ;; "") ;; *) echo "usage: $0 [--dry-run|--undo]" >&2; exit 2 ;; esac
[[ ${mode} == dry || ${EUID} -eq 0 ]] || { echo "run with sudo (or use --dry-run)" >&2; exit 1; }

declare -a TARGETS=() WANT=()
add() { [[ -e "$1" ]] && { TARGETS+=("$1"); WANT+=("$2"); }; return 0; }
for g in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor; do add "${g}" performance; done
for e in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/energy_performance_preference; do add "${e}" performance; done
add /proc/sys/kernel/split_lock_mitigate 0
add /proc/sys/vm/max_map_count 2147483642
add /proc/sys/kernel/sched_autogroup_enabled 0
add /sys/kernel/mm/transparent_hugepage/enabled madvise
for q in /sys/block/nvme*/queue/scheduler; do add "${q}" none; done

current() {  # the active word of a sysfs list like "always [madvise] never", else the value
  local v; v=$(<"$1"); if [[ ${v} == *"["* ]]; then v=${v#*[}; v=${v%%]*}; fi; echo "${v}"
}

if [[ ${mode} == undo ]]; then
  [[ -f ${STATE} ]] || { echo "nothing recorded in ${STATE}"; exit 0; }
  while IFS=$'\t' read -r path old; do echo "${old}" > "${path}" && echo "restored ${path} = ${old}"; done < "${STATE}"
  rm -f "${STATE}"; exit 0
fi

[[ ${mode} == apply && ! -f ${STATE} ]] && : > "${STATE}"
for i in "${!TARGETS[@]}"; do
  p=${TARGETS[$i]}; new=${WANT[$i]}; old=$(current "${p}")
  [[ ${old} == "${new}" ]] && continue
  printf '%-70s %s -> %s\n' "${p}" "${old}" "${new}"
  [[ ${mode} == apply ]] || continue
  if echo "${new}" > "${p}" 2>/dev/null; then printf '%s\t%s\n' "${p}" "${old}" >> "${STATE}"; else echo "  (refused, left as it was)"; fi
done
[[ ${mode} == apply ]] && echo "applied; undo with: sudo $0 --undo (or reboot)"
exit 0
