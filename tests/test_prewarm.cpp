#include "prewarm.hpp"

#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace fleetwm::prewarm;
namespace fs = std::filesystem;

TEST(Prewarm, ManifestRoundTripsAndSkipsBadLines) {
  const std::vector<Range> in = {{"/usr/lib/libfoo.so", 0, 8192}, {"/usr/share/fonts/a.ttf", 4096, 4096}};
  EXPECT_EQ(parse_manifest(format_manifest(in)), in);
  const std::string messy = "relative/path\t0\t4096\n/ok\t0\t4096\n/no-length\t0\t0\n/bad\tx\t4096\n/short\t0\n\n/ok2\t8192\t4096\n";
  const auto out = parse_manifest(messy);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].path, "/ok");
  EXPECT_EQ(out[1].offset, 8192u);
  EXPECT_TRUE(format_manifest({{"/has\ttab", 0, 4096}, {"rel", 0, 4096}, {"/zero", 0, 0}}).empty());
}

TEST(Prewarm, CoalesceJoinsNeighboursWithinTheGapAndCapsTheTotal) {
  std::vector<Range> in = {{"/a", 8192, 4096}, {"/a", 0, 4096}, {"/a", 4096, 4096}, {"/a", 200000, 4096}, {"/b", 0, 4096}};
  const auto out = coalesce(in, 16384, 1u << 30);
  ASSERT_EQ(out.size(), 3u);
  EXPECT_EQ(out[0], (Range{"/a", 0, 12288}));  // three touching pages become one range
  EXPECT_EQ(out[1], (Range{"/a", 200000, 4096}));  // too far away to join
  EXPECT_EQ(out[2].path, "/b");
  const auto close_gap = coalesce({{"/a", 0, 4096}, {"/a", 20480, 4096}}, 16384, 1u << 30);
  EXPECT_EQ(close_gap.size(), 1u) << "a gap of exactly max_gap is read through";
  EXPECT_EQ(close_gap[0].length, 24576u);
  const auto capped = coalesce({{"/a", 0, 1 << 20}, {"/b", 0, 1 << 20}}, 0, 1536 * 1024);
  uint64_t total = 0;
  for (const Range& r : capped) total += r.length;
  EXPECT_EQ(total, 1536u * 1024);
}

TEST(Prewarm, ResidentRangesCoverThePagesThisProcessTouched) {
  const fs::path file = fs::temp_directory_path() / ("fleetwm-prewarm-" + std::to_string(getpid()));
  {
    std::ofstream out(file, std::ios::binary);
    std::string page(4096, 'x');
    for (int i = 0; i < 64; ++i) out << page;
  }
  const int fd = open(file.c_str(), O_RDONLY);
  ASSERT_GE(fd, 0);
  void* m = mmap(nullptr, 64 * 4096, PROT_READ, MAP_PRIVATE, fd, 0);
  ASSERT_NE(m, MAP_FAILED);
  volatile char sink = static_cast<char*>(m)[40 * 4096];  // fault one page in
  (void)sink;
  const auto ranges = resident_file_ranges();
  bool covered = false;
  uint64_t mine = 0;
  for (const Range& r : ranges)
    if (r.path == file.string()) {
      mine += r.length;
      if (r.offset <= 40 * 4096 && 40 * 4096 < r.offset + r.length) covered = true;
    }
  EXPECT_TRUE(covered) << "the touched page must be in the manifest";
  EXPECT_LE(mine, 64u * 4096) << "and nothing beyond the file";
  EXPECT_GT(mine, 0u);
  munmap(m, 64 * 4096);
  close(fd);
  fs::remove(file);
  // and the process's own libraries show up, so a real program gets a real manifest
  bool has_libc = false;
  for (const Range& r : ranges)
    if (r.path.find("libc") != std::string::npos) has_libc = true;
  EXPECT_TRUE(has_libc);
}

TEST(Prewarm, ReplayAsksOnceForEachRangeAndSkipsMissingFiles) {
  const fs::path file = fs::temp_directory_path() / ("fleetwm-prewarm-r-" + std::to_string(getpid()));
  { std::ofstream(file) << std::string(20000, 'y'); }
  EXPECT_EQ(replay({{file.string(), 0, 4096}, {file.string(), 8192, 4096}, {"/nonexistent/fleetwm", 0, 4096}}), 2u);
  fs::remove(file);
}

TEST(Prewarm, FileHeadsAddTheStartOfEveryFileOnce) {
  const fs::path file = fs::temp_directory_path() / ("fleetwm-prewarm-h-" + std::to_string(getpid()));
  { std::ofstream(file) << std::string(10000, 'z'); }
  const auto heads = file_heads({{file.string(), 8192, 4096}, {file.string(), 0, 4096}, {"/nonexistent/x", 0, 4096}}, 256 * 1024);
  ASSERT_EQ(heads.size(), 1u);
  EXPECT_EQ(heads[0].offset, 0u);
  EXPECT_EQ(heads[0].length, 12288u) << "the whole of a small file, rounded up to whole pages";
  fs::remove(file);
}

TEST(Prewarm, AParentFindsTheManifestOfTheProgramItIsAboutToStart) {
  const fs::path cache = fs::temp_directory_path() / ("fleetwm-prewarm-c-" + std::to_string(getpid()));
  const fs::path exe = cache / "prog";
  fs::create_directories(cache);
  { std::ofstream(exe) << "binary"; }
  setenv("XDG_CACHE_HOME", cache.c_str(), 1);
  EXPECT_EQ(prewarm_program(exe.string(), "prog"), 0u) << "no manifest yet";
  const std::string mf = manifest_path_for(exe.string(), "prog");
  ASSERT_FALSE(mf.empty());
  fs::create_directories(fs::path(mf).parent_path());
  { std::ofstream(mf) << exe.string() << "\t0\t4096\n/nonexistent/lib.so\t0\t4096\n"; }
  EXPECT_EQ(prewarm_program(exe.string(), "prog"), 1u) << "one range was readable, the missing file is skipped";
  setenv("FLEETWM_PREWARM", "off", 1);
  EXPECT_EQ(prewarm_program(exe.string(), "prog"), 0u);
  unsetenv("FLEETWM_PREWARM");
  EXPECT_TRUE(manifest_path_for("/nonexistent/exe", "prog").empty());
  unsetenv("XDG_CACHE_HOME");
  fs::remove_all(cache);
}

TEST(Prewarm, ManifestIsKeyedByProgramAndExecutable) {
  setenv("XDG_CACHE_HOME", "/tmp/fleetwm-cache-test", 1);
  const std::string a = manifest_path("fleetwm-bar"), b = manifest_path("fleetwm");
  EXPECT_EQ(a.rfind("/tmp/fleetwm-cache-test/fleetwm/prewarm/fleetwm-bar-", 0), 0u);
  EXPECT_NE(a, b);
  EXPECT_EQ(a.substr(a.size() - 9), ".manifest");
  unsetenv("XDG_CACHE_HOME");
}
