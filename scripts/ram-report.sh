#!/usr/bin/env bash
# ram-report.sh: where does the "cached" (yellow in htop) memory come from?  Read-only: changes nothing, needs no root
# (more is visible with sudo: other users' processes, some files).
#
#   scripts/ram-report.sh              the report on stdout
#   scripts/ram-report.sh > report.txt save it to send or compare (run it again after the change you are testing)
#
# What htop's colours mean (htop's own legend): green = memory used by programs, blue = buffers, yellow/orange = page cache
# (file data the kernel keeps in RAM because it might be read again; it is given back when a program needs memory, which is what
# "available" counts). Cache that is NOT free to give back: tmpfs/shared memory (files in /dev/shm, /tmp on tmpfs, memfd buffers
# that programs share) and dirty pages not yet written. This report separates those from the ordinary, reclaimable cache.
set -uo pipefail

kb() { awk -v k="$1" '$1==k":" {printf "%d", $2}' /proc/meminfo; }
mb() { awk -v v="$1" 'BEGIN{printf "%8.1f MB", v/1024}'; }
section() { printf '\n== %s ==\n' "$1"; }

section "The numbers (from /proc/meminfo)"
total=$(kb MemTotal); free=$(kb MemFree); avail=$(kb MemAvailable); buffers=$(kb Buffers); cached=$(kb Cached)
shmem=$(kb Shmem); mapped=$(kb Mapped); anon=$(kb AnonPages); slab=$(kb Slab); srecl=$(kb SReclaimable); sunrecl=$(kb SUnreclaim)
dirty=$(kb Dirty); wb=$(kb Writeback); swapc=$(kb SwapCached); pt=$(kb PageTables); kstack=$(kb KernelStack)
actf=$(kb "Active(file)"); inactf=$(kb "Inactive(file)")
printf '%-34s %s\n' "Total RAM" "$(mb "$total")"
printf '%-34s %s\n' "Free (nothing in it)" "$(mb "$free")"
printf '%-34s %s\n' "Available (free + what can be given back)" "$(mb "$avail")"
printf '%-34s %s\n' "Cached (page cache, incl. shmem)" "$(mb "$cached")"
printf '%-34s %s\n' "  of which Shmem (tmpfs, memfd, shm)" "$(mb "$shmem")   <- NOT freely reclaimable (it is swap-backed)"
printf '%-34s %s\n' "  of which mapped by programs now" "$(mb "$mapped")   <- libraries and files programs have mapped"
printf '%-34s %s\n' "  Active(file) / Inactive(file)" "$(mb "$actf") / $(mb "$inactf")"
printf '%-34s %s\n' "Buffers" "$(mb "$buffers")"
printf '%-34s %s\n' "Anonymous (programs' own memory)" "$(mb "$anon")"
printf '%-34s %s\n' "Slab (kernel objects)" "$(mb "$slab")   reclaimable $(mb "$srecl"), not $(mb "$sunrecl")"
printf '%-34s %s\n' "Page tables + kernel stacks" "$(mb $((pt + kstack)))"
printf '%-34s %s\n' "Dirty / being written" "$(mb "$dirty") / $(mb "$wb")"
printf '%-34s %s\n' "Swap cache" "$(mb "$swapc")"
plain=$((cached - shmem))
printf '\nOrdinary, reclaimable cache (Cached - Shmem): %s = %d%% of RAM\n' "$(mb "$plain")" $((plain * 100 / total))
printf 'Cache that is stuck (Shmem): %s = %d%% of RAM\n' "$(mb "$shmem")" $((shmem * 100 / total))
printf 'What htop calls "used" (green): %s\n' "$(mb $((total - free - buffers - cached - srecl)))"
if [ "$avail" -gt $((total / 4)) ]; then
  echo "Reading: more than a quarter of RAM is available, so the cache is working, not hurting."
else
  echo "Reading: less than a quarter of RAM is available; look at the sections below for what holds it."
fi

section "Memory-related kernel settings"
for f in vm/swappiness vm/vfs_cache_pressure vm/dirty_ratio vm/dirty_background_ratio vm/watermark_scale_factor vm/min_free_kbytes vm/overcommit_memory; do
  printf '%-34s %s\n' "$f" "$(cat /proc/sys/$f 2>/dev/null || echo n/a)"
done
printf '%-34s %s\n' "zswap enabled" "$(cat /sys/module/zswap/parameters/enabled 2>/dev/null || echo n/a)"
printf '%-34s %s\n' "transparent hugepages" "$(cat /sys/kernel/mm/transparent_hugepage/enabled 2>/dev/null || echo n/a)"
if [ -r /proc/swaps ]; then echo; cat /proc/swaps; fi

section "tmpfs and shared memory (counted as cache, not reclaimable)"
df -h -t tmpfs -t devtmpfs 2>/dev/null | awk 'NR==1 || $3 != "0"' | head -20
echo
echo "Largest things in /dev/shm, /tmp, /run/user (tmpfs-backed paths):"
du -xsk /dev/shm /tmp /run/user/* /var/tmp 2>/dev/null | sort -rn | head -8 | awk '{printf "  %8.1f MB  %s\n", $1/1024, $2}'

section "Programs holding the most mapped file memory (Pss from files, shared fairly between users of the same file)"
{
  for d in /proc/[0-9]*; do
    [ -r "$d/smaps_rollup" ] || continue
    pid=${d#/proc/}
    awk -v pid="$pid" -v comm="$(tr -d '\0' < "$d/comm" 2>/dev/null)" '
      /^Pss:/ {pss=$2} /^Pss_File:/ {pf=$2} /^Pss_Anon:/ {pa=$2} /^Pss_Shmem:/ {ps=$2}
      END {if (pss>0) printf "%d %d %d %d %s %s\n", pf, pa, ps, pss, pid, comm}' "$d/smaps_rollup" 2>/dev/null
  done
} | sort -rn | head -15 | awk 'BEGIN{printf "  %10s %10s %10s %10s  %s\n","file MB","anon MB","shmem MB","Pss MB","process"} {printf "  %10.1f %10.1f %10.1f %10.1f  %s (%s)\n",$1/1024,$2/1024,$3/1024,$4/1024,$6,$5}'

section "Fleetwm's own processes"
for name in fleetwm fleetwm-bar fleetwm-wallpaper fleetwm-lockapplet fleetwm-launcher fleetwm-settings fleetwm-audiomixer fleetwm-greet; do
  for pid in $(pgrep -x "${name:0:15}" 2>/dev/null); do
    awk -v n="$name" -v pid="$pid" '/^Rss:/ {r=$2} /^Pss:/ {p=$2} /^Pss_File:/ {pf=$2} /^Pss_Anon:/ {pa=$2}
      END {printf "  %-20s pid %-7s Rss %7.1f MB  Pss %7.1f MB (file %.1f, anon %.1f)\n", n, pid, r/1024, p/1024, pf/1024, pa/1024}' "/proc/$pid/smaps_rollup" 2>/dev/null
  done
done

section "Biggest files in the page cache (needs fincore from util-linux)"
if command -v fincore >/dev/null 2>&1; then
  echo "Scanning files over 8 MB under /usr /opt /var /home /root /snap /nix (first 20000 candidates) ..."
  {
    find /usr /opt /var /home /root /snap /nix -xdev -type f -size +8M 2>/dev/null | head -20000 | xargs -d '\n' -r fincore -b -n -o RES,SIZE,FILE 2>/dev/null
  } | sort -rn | head -25 | awk '$1 > 0 {printf "  cached %8.1f MB of %8.1f MB  %s\n", $1/1048576, $2/1048576, $3}'
  echo
  echo "Cache held by the files Fleetwm starts from (libraries and binaries it maps right now):"
  for pid in $(pgrep -x fleetwm 2>/dev/null | head -1); do
    awk '$6 ~ /^\// {print $6}' "/proc/$pid/maps" 2>/dev/null | sort -u | xargs -r fincore -b -n -o RES,FILE 2>/dev/null | sort -rn | head -8 |
      awk '$1 > 0 {printf "  cached %8.1f MB  %s\n", $1/1048576, $2}'
  done
else
  echo "fincore is not installed (package util-linux has it on most systems; try: sudo apt install util-linux)."
fi

section "Slab: which kernel caches are big"
if [ -r /proc/slabinfo ]; then
  awk 'NR>2 {printf "%d %s\n", $3*$4/1024, $1}' /proc/slabinfo 2>/dev/null | sort -rn | head -10 | awk '{printf "  %8.1f MB  %s\n", $1/1024, $2}'
else
  echo "(/proc/slabinfo needs root; run: sudo scripts/ram-report.sh)"
fi

section "Things to try, one at a time, and what each tells you"
cat <<'TXT'
  1. Is it really a problem? If "Available" above is large, yellow in htop is just the kernel being useful.
  2. Mostly Shmem? Look at the tmpfs section: a big /tmp, /dev/shm or per-user runtime directory is held in RAM until deleted.
  3. Mostly ordinary cache? Then it is files read once (a big copy, a build, a backup, thumbnails, logs). The "biggest files" list names them.
  4. To see how much is really reclaimable (a measurement, not a fix), and only with the owner's OK because it makes the next
     reads slower for a moment:
         sync; echo 1 | sudo tee /proc/sys/vm/drop_caches     # page cache only; 2 = dentries/inodes; 3 = both
     Run the report again: the drop in Cached is the reclaimable part; what stays is Shmem plus pages programs have mapped.
  5. If the cache comes back at once after the drop, a program is rereading the same data; compare the "Programs holding" lists.
TXT
