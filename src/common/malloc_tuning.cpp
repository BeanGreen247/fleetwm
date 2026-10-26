#include "malloc_tuning.hpp"

#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace fleetwm {

void tune_malloc_for_low_rss() {
#ifdef __GLIBC__  // musl has no mallopt: its allocator already returns freed memory and has no per-thread arenas
  mallopt(M_TRIM_THRESHOLD, 64 * 1024);
  mallopt(M_MMAP_THRESHOLD, 64 * 1024);
  mallopt(M_ARENA_MAX, 2);  // a helper's few threads share one extra arena instead of each growing its own heap
#endif
}

}  // namespace fleetwm
