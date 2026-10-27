#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <sys/mman.h>

#include "page_release.hpp"

namespace {

long rss_file_kb() {
  FILE* f = std::fopen("/proc/self/smaps_rollup", "r");
  long v = -1;
  char line[256];
  while (f && std::fgets(line, sizeof line, f))
    if (std::sscanf(line, "Rss: %ld", &v) == 1) break;
  if (f) std::fclose(f);
  return v;
}

}  // namespace

TEST(PageRelease, DropCodeKeepsTheProgramRunning) {
  volatile double acc = 0;
  fleetwm::drop_code_mappings();  // libc and libm code is unmapped; the next call faults it back in
  for (int i = 1; i < 1000; ++i) acc = acc + std::sqrt(static_cast<double>(i));
  EXPECT_GT(acc, 0.0);
  EXPECT_NE(std::strstr("page_release", "release"), nullptr);
}

TEST(PageRelease, PageOutKeepsHeapAndDataIntact) {
  std::vector<unsigned char> heap(4 << 20);
  for (size_t i = 0; i < heap.size(); ++i) heap[i] = static_cast<unsigned char>(i * 31u);
  static int data[1024];
  for (int i = 0; i < 1024; ++i) data[i] = i * 7;
  fleetwm::page_out_idle_process();
  for (size_t i = 0; i < heap.size(); ++i) ASSERT_EQ(heap[i], static_cast<unsigned char>(i * 31u));
  for (int i = 0; i < 1024; ++i) ASSERT_EQ(data[i], i * 7);
}

TEST(PageRelease, SharedMappingsAreLeftAlone) {
  void* p = mmap(nullptr, 1 << 16, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  ASSERT_NE(p, MAP_FAILED);
  std::memset(p, 0x5a, 1 << 16);
  fleetwm::page_out_idle_process();
  fleetwm::drop_code_mappings();
  EXPECT_EQ(static_cast<unsigned char*>(p)[12345], 0x5a);  // MADV_DONTNEED on this would have zeroed it
  munmap(p, 1 << 16);
}

TEST(PageRelease, DropCodeLowersResidentFileMemory) {
  const long before = rss_file_kb();
  fleetwm::drop_code_mappings();
  const long after = rss_file_kb();
  ASSERT_GE(before, 0);
  EXPECT_LE(after, before);
}
