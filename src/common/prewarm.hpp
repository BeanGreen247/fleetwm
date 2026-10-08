#pragma once

// Start-up prewarm. A program that starts cold (its files are not in the page cache) pays for the reads before and during main(), so
// the reading has to be asked for BEFORE it starts: by its parent.
//
//   learn:   the first run of a program notes which pages of which files it mapped while starting (/proc/self/pagemap), plus the first
//            256 KB of each of those files (the dynamic loader's headers, symbol tables and relocations are read before any page is
//            "mapped"), and writes them to a manifest in ~/.cache/fleetwm/prewarm/ four seconds after it started (start()).
//   replay:  a program that is going to start another one (the compositor starting the bar and the wallpaper) asks the kernel to read
//            that manifest's ranges ahead, asynchronously (posix_fadvise WILLNEED), as early as it can, so the files are in the page cache by
//            the time of the fork (prewarm_program()). Reading ahead inside the started program itself was measured and does not help: it
//            starts too late and costs more than it saves.
//   The manifest is keyed by the executable's size and modification time, so a rebuilt or reinstalled program learns again.
//
//   FLEETWM_PREWARM=off     do nothing at all
//   FLEETWM_PREWARM=record  record again at +4 s even when a manifest exists (replaces it)
//   anything else / unset   record once when there is no manifest
//
// Costs (measured, docs/PERFORMANCE_FINDINGS.md section 14): one short-lived thread in a first run, a few dozen fadvise calls in the parent.
// The page cache gains at most the manifest (about 12 MB for the bar, most of it libraries other programs share). Nothing is written
// outside the user's cache directory.

#include <cstdint>
#include <string>
#include <vector>

namespace fleetwm::prewarm {

struct Range {
  std::string path;  // absolute
  uint64_t offset = 0, length = 0;  // bytes, page aligned
  bool operator==(const Range&) const = default;
};

// Manifest text: one "path<TAB>offset<TAB>length" line per range. Lines that do not parse (relative path, tab or newline in the path,
// zero length) are skipped, so a damaged file never does more than lose ranges.
std::string format_manifest(const std::vector<Range>& ranges);
std::vector<Range> parse_manifest(const std::string& text);

// Sorts by path and offset, then joins ranges of one file that touch or lie within `max_gap` bytes of each other (reading the gap
// is cheaper than a second request), and keeps at most `max_total` bytes in all (the earliest ranges of each file win).
std::vector<Range> coalesce(std::vector<Range> ranges, uint64_t max_gap, uint64_t max_total);

// File pages this process has mapped into its address space right now: every file-backed mapping of /proc/self/maps, filtered
// through /proc/self/pagemap's "present" bit. `maps_path` and `pagemap_path` are parameters so the tests can point them elsewhere.
std::vector<Range> resident_file_ranges(const std::string& maps_path = "/proc/self/maps",
                                        const std::string& pagemap_path = "/proc/self/pagemap");

// Asks the kernel to read each range ahead; returns how many requests were made.
size_t replay(const std::vector<Range>& ranges);

// Where the manifest of `program` as installed at `exe_path` lives ("" when there is no cache directory or no such executable).
// The one-argument form uses the running executable.
std::string manifest_path_for(const std::string& exe_path, const std::string& program);
std::string manifest_path(const std::string& program);

// For a program about to be started by this one: reads its manifest (if the user has one for that build) and asks for the ranges.
// Returns the number of requests, 0 when there was nothing to do.
size_t prewarm_program(const std::string& exe_path, const std::string& program);

// The call a program makes first thing in main() (after block_quit_signals(), before it starts its own threads): learns the manifest
// when there is none.
void start(const char* program);

// The first `bytes` of every file named in `ranges`, as extra ranges (what the loader needs before any page is mapped).
std::vector<Range> file_heads(const std::vector<Range>& ranges, uint64_t bytes);

// Same for several programs, on one detached helper thread at idle I/O priority, so the caller (the compositor, which is itself reading its
// own files from a cold disk at that moment) is not held up by the requests. (Measured on the Celeron laptop, docs/PERFORMANCE_FINDINGS.md
// section 15: asking from the main thread delayed the bar's start by 36 ms; from this thread the delay is gone.) Entries are {executable path, program name}.
void prewarm_programs_async(std::vector<std::pair<std::string, std::string>> programs);

}  // namespace fleetwm::prewarm
