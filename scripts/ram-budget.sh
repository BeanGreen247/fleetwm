#!/usr/bin/env bash
# ram-budget.sh: how much memory the desktop really uses, split into Fleetwm, other programs and the kernel, against a budget.
# Read-only. Uses PSS (proportional set size: shared libraries are split between the programs that map them, so the total adds up
# to what is really used). Run it as root to see every process, as a user to see your own.
#
#   scripts/ram-budget.sh [BUDGET_MB]      # default budget 200
set -uo pipefail
budget=${1:-200}

tmp=$(mktemp); trap 'rm -f "$tmp"' EXIT
for p in /proc/[0-9]*; do
  pss=$(awk '/^Pss:/{print $2}' "$p/smaps_rollup" 2>/dev/null) || continue
  [ -z "$pss" ] && continue
  printf '%s %s\n' "$pss" "$(cat "$p/comm" 2>/dev/null)"
done | sort -rn > "$tmp"

kb() { awk -v k="$1" '$1==k":" {print $2}' /proc/meminfo; }
total=$(kb MemTotal); free=$(kb MemFree); avail=$(kb MemAvailable); anon=$(kb AnonPages); slab_u=$(kb SUnreclaim); pt=$(kb PageTables); ks=$(kb KernelStack); shmem=$(kb Shmem)
mb() { awk -v v="$1" 'BEGIN{printf "%7.1f MB", v/1024}'; }

fleet=$(awk '$2 ~ /^fleetwm/ {s+=$1} END{print s+0}' "$tmp")
others=$(awk '$2 !~ /^fleetwm/ {s+=$1} END{print s+0}' "$tmp")
kernel=$((slab_u + pt + ks))
used=$((total - avail))

echo "== Memory against a ${budget} MB budget =="
printf 'Fleetwm programs (compositor, bar, desktop, applets, remote ...) %s   (%s processes)\n' "$(mb "$fleet")" "$(awk '$2 ~ /^fleetwm/' "$tmp" | wc -l)"
printf 'Everything else running (services, other apps)                %s\n' "$(mb "$others")"
printf 'Kernel memory that cannot be given back                       %s\n' "$(mb "$kernel")"
printf 'In use by the system (total - available)                      %s\n' "$(mb "$used")"
printf 'Fleetwm share of the budget                                   %s of %s MB = %d%%\n' "$(mb "$fleet")" "$budget" $((fleet * 100 / (budget * 1024)))
if [ $((used / 1024)) -le "$budget" ]; then echo "System use is under the budget."; else
  echo "System use is $(( used / 1024 - budget )) MB over the budget. The biggest programs that are not Fleetwm:"
fi
echo
echo "== Biggest programs that are not Fleetwm (PSS) =="
awk '$2 !~ /^fleetwm/ {a[$2]+=$1; n[$2]++} END{for (k in a) printf "%d %s %d\n", a[k], k, n[k]}' "$tmp" | sort -rn | head -12 |
  while read -r k name n; do printf '%s  %s%s\n' "$(mb "$k")" "$name" "$([ "$n" -gt 1 ] && echo " (x$n)")"; done
echo
echo "== Fleetwm programs =="
awk '$2 ~ /^fleetwm/ {a[$2]+=$1; n[$2]++} END{for (k in a) printf "%d %s %d\n", a[k], k, n[k]}' "$tmp" | sort -rn |
  while read -r k name n; do printf '%s  %s%s\n' "$(mb "$k")" "$name" "$([ "$n" -gt 1 ] && echo " (x$n)")"; done
echo
echo "(Page cache is not counted: the kernel gives it back when a program needs the memory. Shmem/tmpfs now: $(mb "$shmem"))"
