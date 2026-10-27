#pragma once

// Gives back memory a process mapped at start-up and no longer touches. Header-only so the root greeter can use it without linking
// libcommon. Both calls are safe to repeat; anything still in use comes back by itself on the next access.

#include <sys/mman.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifndef MADV_PAGEOUT
#define MADV_PAGEOUT 21
#endif

namespace fleetwm {

namespace page_release_detail {

// Calls fn(start, len, perms, path) for each line of /proc/self/maps.
template <class Fn>
void for_each_mapping(Fn fn) {
  FILE* f = std::fopen("/proc/self/maps", "r");
  if (!f) return;
  char line[1024];
  while (std::fgets(line, sizeof line, f)) {
    unsigned long lo = 0, hi = 0;
    char perms[8] = {0};
    int off = 0;
    if (std::sscanf(line, "%lx-%lx %7s %*s %*s %*s %n", &lo, &hi, perms, &off) < 3) continue;
    std::string path(line + off);
    while (!path.empty() && (path.back() == '\n' || path.back() == ' ')) path.pop_back();
    fn(lo, hi - lo, perms, path);
  }
  std::fclose(f);
}

inline bool plain_file_path(const std::string& p) {
  return !p.empty() && p[0] == '/' && p.compare(0, 7, "/memfd:") != 0 && p.compare(0, 5, "/dev/") != 0 && p.find("(deleted)") == std::string::npos;
}

}  // namespace page_release_detail

// True when a zram device is an active swap area (the cost of paging to it is a decompress, not a disk read).
inline bool has_zram_swap() {
  FILE* f = std::fopen("/proc/swaps", "r");
  if (!f) return false;
  char line[512];
  bool found = false;
  while (std::fgets(line, sizeof line, f))
    if (std::strstr(line, "/zram")) found = true;
  std::fclose(f);
  return found;
}

// Drops this process's page-table entries for the code (r-x, file-backed) of every library, Mesa and LLVM included. The pages stay in the
// page cache; the ones the program still runs come back with a minor fault, the rest stop counting in this process's RSS and PSS.
// Needs no privilege (MADV_PAGEOUT on a file the caller does not own is refused by the kernel). Only read-only executable mappings are
// touched: data and RELRO pages hold relocated values that MADV_DONTNEED would throw away.
inline void drop_code_mappings() {
  page_release_detail::for_each_mapping([](unsigned long lo, unsigned long len, const char* perms, const std::string& path) {
    if (std::strcmp(perms, "r-xp") != 0 || !page_release_detail::plain_file_path(path)) return;
    madvise(reinterpret_cast<void*>(lo), len, MADV_DONTNEED);
  });
}

// Sends the shared-memory buffers named `memfd_name` (as in memfd_create) to zram. A wallpaper is mostly flat or smooth, so zram holds it in a
// small fraction of its size (a solid colour costs a few bytes per page); the compositor reads it back only when the desktop is
// uncovered. Does nothing without zram: paging to a disk or SSD swap would cost a read per exposed page. Allowed because the memfd belongs
// to the same user.
inline void page_out_memfd(const char* memfd_name) {
  if (!has_zram_swap()) return;
  const std::string want = std::string("/memfd:") + memfd_name;
  page_release_detail::for_each_mapping([&](unsigned long lo, unsigned long len, const char* perms, const std::string& path) {
    if (perms[3] != 's' || path.compare(0, want.size(), want) != 0) return;
    madvise(reinterpret_cast<void*>(lo), len, MADV_PAGEOUT);
  });
}

// For a long-idle process (the greeter waiting for a session to end): reclaim every private mapping. Data goes to swap if there is any,
// clean file pages go back to the page cache. Content is preserved, so it is safe for heap and relocated data too. Root only gets
// the file part (see above); anonymous memory is only worth sending to swap when it is zram.
inline void page_out_idle_process() {
  const bool anon_ok = has_zram_swap();
  page_release_detail::for_each_mapping([anon_ok](unsigned long lo, unsigned long len, const char* perms, const std::string& path) {
    if (perms[3] != 'p') return;  // shared mappings (memfd, shm) are other processes' business
    if (path == "[stack]" || path == "[vdso]" || path == "[vvar]" || path == "[vsyscall]") return;
    const bool file = !path.empty() && path[0] == '/';
    if (!file && !anon_ok) return;
    madvise(reinterpret_cast<void*>(lo), len, MADV_PAGEOUT);
  });
}

}  // namespace fleetwm
