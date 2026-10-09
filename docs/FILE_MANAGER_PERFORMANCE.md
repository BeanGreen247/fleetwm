# File manager: what was measured, what was changed, how it compares

2026-10-09, `fleetwm-fm` 0.4.0 (tree on top of `2f7a6fb`). Method and rules: the `developer-performance-opt` skill (baseline first, profile or
count before changing, one change at a time, re-measure, keep only what beats the noise, say what was not measured). Raw output of every
run: `perflog/2026-10-09/`; the programs that produced it: `dirbench.cpp`, `copybench.cpp`, `uibench.cpp`, `uicompare.sh`
(`meson setup -Dbenchmarks=true`, never installed).

## Setup

- Intel Core i5-8500 (this VM has **2** vCPUs), no SHA instructions, Debian 13, kernel 6.12, GCC 14.2.
- Build: meson `--buildtype=release` (`-O3`), no LTO, no PGO, no `-march=native` (`install.sh` adds those on the owner's machines;
  section "Flags" below shows they change nothing for this code).
- Disk: ext4 on a virtual disk (`dd` 1 GiB write + fsync: 6.6 to 9.8 s across runs, 110 to 162 MB/s; O_DIRECT read 2.1 GB/s) and tmpfs.
- **The VM is also the owner's desktop** (VNC session, Brave, Lestrix at 35 % CPU): CPU-bound numbers repeat within a few percent, disk-bound
  numbers swing by up to 2x between runs. Where it matters, the range is given instead of one figure. An earlier set of runs overlapped with a sanitizer build and was
  discarded and repeated on a quiet machine.
- Test data: `d1000`, `d10000`, `d100000` (every 7th entry a folder, empty files), a 1 GiB random file, 20,000 files of 4 KiB.

## 1. Opening a big folder

Speed of light: `getdents64` returns ~100 names per 4 KiB of dirents, so 100,000 names are a few hundred system calls; the cost is the
kernel's dentry walk, measured at **0.28 µs per entry** (28 ms for 100,000, `fm-names`). A `statx` per entry costs about 2 µs (the
dominant cost of the first version). Sorting 100,000 names: 100,000 x 17 comparisons at a few ns is about 10 ms in the best case.

| 100,000 entries (ext4), median of 15 | before | after | std::filesystem + stat + same sort | readdir + lstat |
|---|---|---|---|---|
| list + sort, ready to draw | 232 ms | **84 ms** | 438 ms | 226 ms |
| 10,000 entries | 21.7 ms | **6.3 ms** | 42.7 ms | 22.8 ms |
| 1,000 entries | 1.9 ms | **0.5 ms** | 3.9 ms | 2.0 ms |

(`01-dirbench.txt`, `02-dirbench-lazy.txt`, `03-dirbench-sortkeys.txt`.) tmpfs: 187 ms -> 58 ms at 100,000.

Changes, in the order they were made, each kept only because it measured better:

1. **Stat only what is drawn.** The folder is read with names and kinds (`d_type`); size, date and mode are read with `statx` for the rows that
   are drawn (`Browser::ensure_stat_range`), and for everything in a background pass only when something needs them all (sort by size or date, a
   selection that reaches rows never drawn). 232 -> 96 ms. Symlinks are still stat'ed at once (a link to a folder must open like one).
2. **Sort by precomputed byte keys** (`append_natural_key`): digit runs become a length plus the digits, letters are folded, so one `memcmp`
   orders names exactly like the natural comparison. A differential test compares the key order with `natural_compare` on 700 random names
   (490,000 pairs). Sort of 100,000 names 64 -> 47 ms (keys 4 ms, sort 32 ms, permute 2 ms). The sort is now 40 % of the open time; a
   prefix-key array that fits the cache is the next step and would be worth about 20 ms (estimated, not built: 84 ms is already under the
   100 ms mark where a delay starts to be felt).
   **Built 2026-10-09:** sort records carry the first 16 key bytes; 100,300 names list + sort 93-98 ms -> 59 ms (`perflog/2026-10-09/12-sort-prefix-ab.txt`); an 8-byte prefix gained only 5-9 ms because names like `IMG_00123` share their first 7 key bytes.

## 2. Drawing

`uibench` (`04-uibench.txt`): the median frame of a full 1024x768 repaint in software cairo is **1.1 to 1.6 ms in every view mode, with 1,000 or
with 100,000 entries**, at the top of the list, while scrolling by jumps, and while the pointer moves. Only the visible rows are drawn (the
metrics give the first and last index; hit testing is arithmetic), text widths are cached, icons are drawn once per kind and size into a cache, and
pictures are decoded on a worker thread.

**Idle cost is zero**: nothing redraws unless something changed (the window asks for a frame; there is no timer except a 3-second volume
check while focused and a 250 ms tick only while a transfer runs). Measured in the real compositor: 0.00 CPU seconds over 5 idle seconds.

## 3. Whole program, against Caja

`uicompare.sh` (`09-uicompare.txt`): `fleetwm-fm` in Fleetwm's own compositor (headless, pixman) and Caja 1.26.4 (GTK3 + GVfs) under Xvfb, same
folder, same sampling: start, wait 8 to 10 s, read `/proc/<pid>/status` and `/proc/<pid>/stat`, wait 5 s, read the CPU time again.

| | folder with 1,000 files | folder with 100,000 files |
|---|---|---|
| fleetwm-fm resident memory | **25.7 MB** (peak 25.7) | **34.9 MB** (peak 40) |
| Caja resident memory | 57.8 MB (peak 75.8) | 58.2 MB (peak 76.1) |
| fleetwm-fm CPU to settle | **0.02 s** | **0.14 s** |
| Caja CPU to settle (second, warm run) | 0.28 s | 0.28 s |
| threads | 2 | 2 |
| Caja threads | 5 | 5 |
| CPU over 5 idle seconds | 0.00 s | 0.00 s |

Caja's first, cold run needed 1.94 s of CPU. Caja's figures do not include the GVfs daemons it starts (volume monitors, metadata, `gvfsd`), and
`fleetwm-fm` starts none. Process start to a written PNG, offline (`--screenshot`: dynamic loading, fontconfig, a 1,000-file listing, one frame,
PNG encode): 68 ms (`10-startup-offline.txt`). Time to first frame inside a compositor was not measured (no way to timestamp it from outside).

**Not compared:** PCManFM (not installed, and `apt` needs a password this session does not have), Nemo, Thunar, Dolphin, Nautilus, Windows
Explorer. Do not read these numbers as a claim about them. Caja does not check copies, so no copy comparison with it is possible.

## 4. Copying and checking

A verified copy has a floor: the disk must take the bytes (`dd` write + fsync), then give them back, and the data must be hashed twice. Floor on
this VM for 1 GiB: 6.6 to 9.8 s (write) + 0.5 s (read) + 2 x 1.3 s (BLAKE2b at 841 MB/s; SHA-256 would be 2 x 2.3 s) = about **10 to 13 s**.

Two real problems were found and fixed (`05-copybench-big.txt`, `06-copybench-small-before.txt`, `08-copybench-after.txt`):

1. **20,000 small files took 529 s verified** (26 ms per file: an `fsync` and a direct read-back for each one, on a virtual disk), against 0.8 s for
   `cp -r` and 1.5 s unverified. Files below 64 MiB are now written under temporary names and flushed with **one `syncfs` per batch**
   (256 MiB or 4,096 files), read back by up to eight threads (a small read-back is one round trip to the device, so several in flight hide it),
   then renamed. Verified: **4.8 to 5.0 s** (3 runs). Unverified: 1.3 s (cp -r 0.7 to 0.8 s: the temporary name, the rename, the conflict check and
   the progress cost about 30 µs per file). Also removed: a 1 MiB aligned allocation per file, an unneeded `fallocate` below 1 MiB, a lock per block.
2. **1 GiB verified: 14.6 s -> about 10.7 s in the best runs** (11 to 22 s in others, the disk dominating): the device is told to start writing
   every 16 MiB (`sync_file_range`) so the final `fsync` has little left to wait for, and the checksum is chosen by measurement
   (`Automatic`: SHA-256, SHA-512 or BLAKE2b, whichever is fastest here; on this CPU without SHA instructions BLAKE2b is 1.8x SHA-256, on a
   CPU with them SHA-256 wins and stays). The choice takes 25 ms once, off the main thread. Unverified 1 GiB: 0.6 to 1.1 s when the page cache has room,
   `copy_file_range`, the same speed as `cp` (0.7 to 1.5 s).

Reference rows: `cp` + `sync` + `sha256sum` of both files, which is what a careful user types, took 13.3 to 15.0 s. The verified copy matches or beats it
when the disk is quiet and loses when the disk stalls (the shared VM disk did, runs of 15 to 29 s). The O_DIRECT read-back is the honest part
(the page cache cannot vouch for the media); `direct_verify = false` reads through the cache and is faster and weaker.

What was **not** done: overlapping the read-back of the first chunks with the writing of the later ones (about 2 s on a fast disk, nothing on a
USB stick whose bus is the limit), and a single pass for tiny files. Decision 2026-10-09: the overlap stays out. An O_DIRECT read of a range that is still dirty in the page cache can see old bytes, so every chunk would need `sync_file_range` (write and wait) before it is read back, which is the fsync cost again in smaller pieces; the upper bound on the 1 GiB rows above is 2 s of 17 s, and the only protection against a false mismatch is the one thing the feature is for.

## 5. Flags

Checked with the non-default flags checklist, on the listing and sort code (`11-flags-ab.txt`, 15 runs x 3 sessions each):

| flags | fm-names | fm-lazy (list + sort) |
|---|---|---|
| `-O2` | 28.4 to 29.8 ms | 85.3 to 88.5 ms |
| `-O3` (meson release, used) | 27.6 to 29.1 ms | 75.5 to 85.3 ms |
| `-O3 -march=native -flto` | 27.1 to 27.3 ms | 74.1 to 78.4 ms |

`-march=native -flto` is inside the `-O3` noise for this memory-bound code; `install.sh`'s LTO, `-march=native` and PGO therefore neither help nor hurt here.
Not tried: PGO training for `fleetwm-fm` (`scripts/pgo-train-session.sh` does not open it; the training run would need folders to list and clicks),
`-fno-semantic-interposition` (static libraries, nothing to interpose), allocator tuning (`tune_malloc_for_low_rss()` is already called).

## 6. Correctness checks for the changes above

(Counts below are from when the measurements were made; the later features raised them to 260 file manager tests, 1,007 in all, with the sanitizers repeated and still clean.)

226 tests in the `Fm*` suites, 973 in the whole project, all passing. Sanitizers on the final code: AddressSanitizer + UndefinedBehaviorSanitizer
226 tests, no report except fontconfig's own 1 KB leak at exit; ThreadSanitizer on the window and transfer suites (78 tests), 0 warnings. A thread bug the
parallel read-back introduced (all threads shared one aligned buffer, 13,972 false mismatches on the 20,000-file run) was caught by the transfer
tests and the benchmark and fixed by giving each thread its own buffer. The checksum key order, the lazy stat, batching, cancellation with files
waiting for their check (nothing left behind), two sources with one name inside a batch, every checksum algorithm and move semantics each have a test.

## 7. Reproduce

```sh
meson setup build-bench --buildtype=release -Dtests=false -Dgreeter=false -Dbenchmarks=true
ninja -C build-bench apps/fleetfm/fleetwm-fm-dirbench apps/fleetfm/fleetwm-fm-copybench apps/fleetfm/fleetwm-fm-uibench apps/fleetfm/fleetwm-fm
build-bench/apps/fleetfm/fleetwm-fm-dirbench DIR 15
build-bench/apps/fleetfm/fleetwm-fm-uibench DIR 200
build-bench/apps/fleetfm/fleetwm-fm-copybench SRC DEST_DIR VERIFY(0|1) DIRECT(0|1)
perflog/2026-10-09/uicompare.sh DIR 8     # needs build/src/compositor/fleetwm and caja
```

## Not measured

Time to first frame inside a compositor; a real GPU (software cairo on the CPU is what the window uses, and the compositor's upload cost
for a shm buffer is in `PERFORMANCE_FINDINGS.md`); the laptop (Celeron) and any real USB stick, SD card, NAS or Nextcloud; battery cost; copy
speed with several jobs at once; folder opening over GVfs/FUSE (network latency); icons-view scrolling with thousands of thumbnails (the decoder
runs on one worker thread and keeps at most 64 MB); PCManFM and the other file managers.
