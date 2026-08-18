#include "malloc_tuning.hpp"

#include <malloc.h>

namespace fleetwm {

void tune_malloc_for_low_rss() {
  mallopt(M_TRIM_THRESHOLD, 64 * 1024);
  mallopt(M_MMAP_THRESHOLD, 64 * 1024);
}

}  // namespace fleetwm
