#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

#include "hasher.hpp"
#include "transfer.hpp"

using namespace fleetwm::fm;
namespace fs = std::filesystem;

namespace {
class FmTransfer : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ = fs::temp_directory_path() / ("fm-transfer-" + std::to_string(::getpid()) + "-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
    src_ = root_ / "src";
    dst_ = root_ / "dst";
    fs::create_directories(src_);
    fs::create_directories(dst_);
  }
  void TearDown() override {
    std::error_code ec;
    fs::remove_all(root_, ec);
  }
  std::string random_file(const fs::path& p, size_t bytes, unsigned seed = 1) {
    std::mt19937 rng(seed);
    std::string data(bytes, 0);
    for (char& c : data) c = static_cast<char>(rng());
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << data;
    return data;
  }
  static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
  }
  fs::path root_, src_, dst_;
};
}  // namespace

TEST_F(FmTransfer, CopiesAFileAndChecksItAgainstTheDevice) {
  const std::string data = random_file(src_ / "a.bin", 3 * 1024 * 1024 + 17);
  Transfer t({(src_ / "a.bin").string()}, dst_.string(), {});
  const TransferResult r = t.run();
  EXPECT_TRUE(r.ok());
  EXPECT_EQ(slurp(dst_ / "a.bin"), data);
  ASSERT_EQ(r.verified.size(), 1u);
  EXPECT_TRUE(r.verified[0].matched);
  EXPECT_EQ(r.verified[0].checksum, hash_file((dst_ / "a.bin").string()));
  EXPECT_EQ(r.bytes_copied, data.size());
  EXPECT_TRUE(fs::exists(src_ / "a.bin"));
  const Progress p = t.progress();
  EXPECT_EQ(p.phase, Phase::Done);
  EXPECT_DOUBLE_EQ(p.fraction(), 1.0);
  EXPECT_EQ(p.verified, p.verify_total);
}

TEST_F(FmTransfer, NoPartFileIsLeftBehind) {
  random_file(src_ / "a.bin", 1000);
  Transfer({(src_ / "a.bin").string()}, dst_.string(), {}).run();
  for (const auto& e : fs::directory_iterator(dst_)) EXPECT_EQ(e.path().filename(), "a.bin") << e.path();
}

TEST_F(FmTransfer, FastPathWithoutVerificationStillCopiesExactly) {
  const std::string data = random_file(src_ / "a.bin", 9 * 1024 * 1024 + 3);
  TransferOptions o;
  o.verify = false;
  const TransferResult r = Transfer({(src_ / "a.bin").string()}, dst_.string(), o).run();
  EXPECT_TRUE(r.ok());
  EXPECT_TRUE(r.verified.empty());
  EXPECT_EQ(slurp(dst_ / "a.bin"), data);
}

TEST_F(FmTransfer, CopiesATreeWithEmptyFilesAndKeepsModeAndTime) {
  random_file(src_ / "tree/one.txt", 100, 2);
  random_file(src_ / "tree/deep/two.txt", 5000, 3);
  random_file(src_ / "tree/deep/empty", 0);
  fs::create_directories(src_ / "tree/emptydir");
  fs::permissions(src_ / "tree/one.txt", fs::perms::owner_all | fs::perms::group_read);
  struct timespec ts[2] = {{0, UTIME_OMIT}, {1500000000, 0}};
  ::utimensat(AT_FDCWD, (src_ / "tree/one.txt").c_str(), ts, 0);
  Transfer t({(src_ / "tree").string()}, dst_.string(), {});
  const TransferResult r = t.run();
  EXPECT_TRUE(r.ok());
  EXPECT_EQ(slurp(dst_ / "tree/deep/two.txt"), slurp(src_ / "tree/deep/two.txt"));
  EXPECT_TRUE(fs::exists(dst_ / "tree/deep/empty"));
  EXPECT_TRUE(fs::is_directory(dst_ / "tree/emptydir"));
  struct stat st;
  ASSERT_EQ(::stat((dst_ / "tree/one.txt").c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 07777, 0740u);
  EXPECT_EQ(st.st_mtim.tv_sec, 1500000000);
  EXPECT_EQ(t.progress().files_total, 3u);
}

TEST_F(FmTransfer, SymlinksStayLinks) {
  random_file(src_ / "real", 10);
  fs::create_symlink("real", src_ / "ln");
  Transfer({(src_ / "ln").string()}, dst_.string(), {}).run();
  EXPECT_TRUE(fs::is_symlink(dst_ / "ln"));
  EXPECT_EQ(fs::read_symlink(dst_ / "ln"), "real");
}

TEST_F(FmTransfer, ConflictKeepBothNamesTheCopyLikeExplorer) {
  random_file(src_ / "a.txt", 10, 1);
  random_file(dst_ / "a.txt", 20, 2);
  TransferOptions o;
  o.on_conflict = [](const ConflictInfo& c) {
    EXPECT_EQ(c.source_size, 10u);
    EXPECT_EQ(c.dest_size, 20u);
    return Conflict::KeepBoth;
  };
  Transfer({(src_ / "a.txt").string()}, dst_.string(), o).run();
  EXPECT_EQ(fs::file_size(dst_ / "a.txt"), 20u);
  EXPECT_EQ(fs::file_size(dst_ / "a (2).txt"), 10u);
}

TEST_F(FmTransfer, ConflictReplaceAndSkip) {
  const std::string d1 = random_file(src_ / "a.txt", 10, 1);
  random_file(dst_ / "a.txt", 20, 2);
  TransferOptions o;
  o.on_conflict = [](const ConflictInfo&) { return Conflict::Replace; };
  Transfer({(src_ / "a.txt").string()}, dst_.string(), o).run();
  EXPECT_EQ(slurp(dst_ / "a.txt"), d1);

  random_file(src_ / "b.txt", 10, 3);
  const std::string keep = random_file(dst_ / "b.txt", 20, 4);
  o.on_conflict = [](const ConflictInfo&) { return Conflict::Skip; };
  const TransferResult r = Transfer({(src_ / "b.txt").string()}, dst_.string(), o).run();
  EXPECT_TRUE(r.ok());
  EXPECT_EQ(slurp(dst_ / "b.txt"), keep);
}

TEST_F(FmTransfer, CancelFromTheConflictQuestionStopsEverything) {
  random_file(src_ / "a.txt", 10);
  random_file(dst_ / "a.txt", 20);
  TransferOptions o;
  o.on_conflict = [](const ConflictInfo&) { return Conflict::Cancel; };
  const TransferResult r = Transfer({(src_ / "a.txt").string()}, dst_.string(), o).run();
  EXPECT_TRUE(r.cancelled);
  EXPECT_EQ(fs::file_size(dst_ / "a.txt"), 20u);
}

TEST_F(FmTransfer, FoldersMergeAndKeepBothForFilesInside) {
  random_file(src_ / "d/x.txt", 10, 1);
  random_file(src_ / "d/y.txt", 10, 2);
  random_file(dst_ / "d/x.txt", 30, 3);
  const TransferResult r = Transfer({(src_ / "d").string()}, dst_.string(), {}).run();
  EXPECT_TRUE(r.ok());
  EXPECT_EQ(fs::file_size(dst_ / "d/x.txt"), 30u);
  EXPECT_EQ(fs::file_size(dst_ / "d/x (2).txt"), 10u);
  EXPECT_TRUE(fs::exists(dst_ / "d/y.txt"));
}

TEST_F(FmTransfer, SkippingAFolderSkipsEverythingInsideIt) {
  random_file(src_ / "d/x.txt", 10, 1);
  random_file(dst_ / "d", 5, 2);  // a file where the folder would go
  TransferOptions o;
  o.on_conflict = [](const ConflictInfo&) { return Conflict::Skip; };
  const TransferResult r = Transfer({(src_ / "d").string()}, dst_.string(), o).run();
  EXPECT_TRUE(fs::is_regular_file(dst_ / "d"));
  EXPECT_FALSE(fs::exists(root_ / "x.txt"));
  EXPECT_TRUE(r.errors.empty());
}

TEST_F(FmTransfer, KeepBothOnAFolderRenamesTheWholeSubtree) {
  random_file(src_ / "d/x.txt", 10, 1);
  random_file(dst_ / "d", 5, 2);  // a file blocks the name
  TransferOptions o;
  o.on_conflict = [](const ConflictInfo&) { return Conflict::KeepBoth; };
  const TransferResult r = Transfer({(src_ / "d").string()}, dst_.string(), o).run();
  EXPECT_TRUE(r.ok());
  EXPECT_TRUE(fs::exists(dst_ / "d (2)/x.txt"));
}

TEST_F(FmTransfer, MoveOnTheSameFilesystemRenames) {
  const std::string data = random_file(src_ / "m.bin", 1000);
  const TransferResult r = Transfer({(src_ / "m.bin").string()}, dst_.string(), [] { TransferOptions o; o.move = true; return o; }()).run();
  EXPECT_TRUE(r.ok());
  EXPECT_FALSE(fs::exists(src_ / "m.bin"));
  EXPECT_EQ(slurp(dst_ / "m.bin"), data);
}

TEST_F(FmTransfer, MoveAcrossConflictCopiesVerifiesThenRemovesTheSource) {
  const std::string data = random_file(src_ / "m.bin", 100000);
  random_file(dst_ / "m.bin", 5);
  TransferOptions o;
  o.move = true;
  o.on_conflict = [](const ConflictInfo&) { return Conflict::Replace; };
  const TransferResult r = Transfer({(src_ / "m.bin").string()}, dst_.string(), o).run();
  EXPECT_TRUE(r.ok());
  EXPECT_FALSE(fs::exists(src_ / "m.bin"));
  EXPECT_EQ(slurp(dst_ / "m.bin"), data);
  ASSERT_EQ(r.verified.size(), 1u);
}

TEST_F(FmTransfer, MovedFolderIsRemovedWhenEmpty) {
  random_file(src_ / "d/x.txt", 10, 1);
  fs::create_directories(dst_ / "d");  // forces copy and merge instead of a plain rename
  TransferOptions o;
  o.move = true;
  const TransferResult r = Transfer({(src_ / "d").string()}, dst_.string(), o).run();
  EXPECT_TRUE(r.ok());
  EXPECT_FALSE(fs::exists(src_ / "d"));
  EXPECT_TRUE(fs::exists(dst_ / "d/x.txt"));
}

TEST_F(FmTransfer, ACopyIntoItselfIsRefused) {
  random_file(src_ / "d/x.txt", 10);
  const TransferResult r = Transfer({(src_ / "d").string()}, (src_ / "d").string(), {}).run();
  EXPECT_FALSE(r.errors.empty());
  EXPECT_FALSE(fs::exists(src_ / "d/d"));
}

TEST_F(FmTransfer, MissingSourceIsAnErrorNotACrash) {
  const TransferResult r = Transfer({(src_ / "gone").string()}, dst_.string(), {}).run();
  ASSERT_EQ(r.errors.size(), 1u);
  EXPECT_FALSE(r.ok());
}

TEST_F(FmTransfer, ProgressReachesTheTotalsAndCountsBothPasses) {
  random_file(src_ / "a", 2 << 20, 1);
  random_file(src_ / "b", 1 << 20, 2);
  Transfer t({(src_ / "a").string(), (src_ / "b").string()}, dst_.string(), {});
  t.run();
  const Progress p = t.progress();
  EXPECT_EQ(p.bytes_total, 3u << 20);
  EXPECT_EQ(p.verify_total, 3u << 20);
  EXPECT_EQ(p.files_done, 2u);
  EXPECT_EQ(p.files_total, 2u);
}

TEST_F(FmTransfer, CancelStopsAndLeavesNoPartFile) {
  random_file(src_ / "big", 64 << 20);
  TransferOptions o;
  o.block_bytes = 64 * 1024;
  Transfer t({(src_ / "big").string()}, dst_.string(), o);
  std::thread canceller([&] {
    for (int i = 0; i < 2000 && t.progress().bytes_done == 0; ++i) usleep(500);
    t.cancel();
  });
  const TransferResult r = t.run();
  canceller.join();
  EXPECT_TRUE(r.cancelled);
  EXPECT_TRUE(fs::is_empty(dst_));
}

TEST_F(FmTransfer, PauseHoldsTheCopyUntilResumed) {
  random_file(src_ / "f", 8 << 20);
  TransferOptions o;
  o.block_bytes = 64 * 1024;
  Transfer t({(src_ / "f").string()}, dst_.string(), o);
  t.pause(true);
  std::thread worker([&] { t.run(); });
  usleep(200 * 1000);
  const uint64_t held = t.progress().bytes_done;
  usleep(100 * 1000);
  EXPECT_EQ(t.progress().bytes_done, held);
  EXPECT_TRUE(t.progress().paused);
  t.pause(false);
  worker.join();
  EXPECT_EQ(t.progress().phase, Phase::Done);
  EXPECT_EQ(fs::file_size(dst_ / "f"), 8u << 20);
}

TEST_F(FmTransfer, UniqueNameCountsUp) {
  EXPECT_EQ(unique_name(dst_.string(), "free.txt"), "free.txt");
  random_file(dst_ / "a.tar.gz", 1);
  EXPECT_EQ(unique_name(dst_.string(), "a.tar.gz"), "a.tar (2).gz");
  random_file(dst_ / "a.tar (2).gz", 1);
  EXPECT_EQ(unique_name(dst_.string(), "a.tar.gz"), "a.tar (3).gz");
  random_file(dst_ / "noext", 1);
  EXPECT_EQ(unique_name(dst_.string(), "noext"), "noext (2)");
  random_file(dst_ / ".hid", 1);
  EXPECT_EQ(unique_name(dst_.string(), ".hid"), ".hid (2)");
}

TEST_F(FmTransfer, CopyNamesFollowExplorer) {
  EXPECT_EQ(copy_name(dst_.string(), "a.txt"), "a - Copy.txt");
  random_file(dst_ / "a - Copy.txt", 1);
  EXPECT_EQ(copy_name(dst_.string(), "a.txt"), "a - Copy (2).txt");
  EXPECT_EQ(copy_name(dst_.string(), "folder"), "folder - Copy");
}

TEST_F(FmTransfer, CopyingIntoItsOwnFolderMakesACopyNextToTheOriginal) {
  const std::string data = random_file(src_ / "a.bin", 1000);
  const TransferResult r = Transfer({(src_ / "a.bin").string()}, src_.string(), {}).run();
  EXPECT_TRUE(r.ok());
  EXPECT_EQ(slurp(src_ / "a.bin"), data);
  EXPECT_EQ(slurp(src_ / "a - Copy.bin"), data);
}

TEST_F(FmTransfer, CopyingAFolderIntoItsParentMakesAFolderCopy) {
  random_file(src_ / "d/x.txt", 10);
  const TransferResult r = Transfer({(src_ / "d").string()}, src_.string(), {}).run();
  EXPECT_TRUE(r.ok()) << (r.errors.empty() ? "" : r.errors[0].message);
  EXPECT_TRUE(fs::exists(src_ / "d - Copy/x.txt"));
}

TEST_F(FmTransfer, MovingAFileOntoItselfDoesNothing) {
  random_file(src_ / "a.bin", 10);
  TransferOptions o;
  o.move = true;
  const TransferResult r = Transfer({(src_ / "a.bin").string()}, src_.string(), o).run();
  EXPECT_TRUE(r.ok());
  EXPECT_TRUE(fs::exists(src_ / "a.bin"));
  EXPECT_EQ(fs::directory_iterator(src_) == fs::directory_iterator(), false);
}

TEST_F(FmTransfer, ManySmallFilesAreCheckedInBatches) {
  for (int i = 0; i < 300; ++i) random_file(src_ / "many" / ("f" + std::to_string(i)), 1000 + static_cast<size_t>(i), static_cast<unsigned>(i));
  TransferOptions o;
  o.batch_files = 64;  // several batches
  const TransferResult r = Transfer({(src_ / "many").string()}, dst_.string(), o).run();
  EXPECT_TRUE(r.ok());
  EXPECT_EQ(r.verified.size(), 300u);
  for (const auto& v : r.verified) EXPECT_TRUE(v.matched);
  for (int i : {0, 77, 299}) EXPECT_EQ(slurp(dst_ / "many" / ("f" + std::to_string(i))), slurp(src_ / "many" / ("f" + std::to_string(i))));
  for (const auto& e : fs::recursive_directory_iterator(dst_)) EXPECT_EQ(e.path().extension() == ".fleetfm-part", false) << e.path();
}

TEST_F(FmTransfer, ABigFileIsCheckedOnItsOwnWhileSmallOnesWait) {
  random_file(src_ / "a-small", 100, 1);
  random_file(src_ / "b-big", 3 << 20, 2);
  random_file(src_ / "c-small", 100, 3);
  TransferOptions o;
  o.big_file_bytes = 1 << 20;
  const TransferResult r = Transfer({(src_ / "a-small").string(), (src_ / "b-big").string(), (src_ / "c-small").string()}, dst_.string(), o).run();
  EXPECT_TRUE(r.ok());
  ASSERT_EQ(r.verified.size(), 3u);
  EXPECT_EQ(r.verified[0].source, (src_ / "b-big").string()) << "the big file was finished first, the small ones at the end";
}

TEST_F(FmTransfer, TwoSourcesWithTheSameNameDoNotClobberEachOtherInABatch) {
  fs::create_directories(src_ / "one");
  fs::create_directories(src_ / "two");
  const std::string d1 = random_file(src_ / "one/same.txt", 500, 1);
  const std::string d2 = random_file(src_ / "two/same.txt", 600, 2);
  TransferOptions o;
  o.on_conflict = [](const ConflictInfo&) { return Conflict::KeepBoth; };
  const TransferResult r = Transfer({(src_ / "one/same.txt").string(), (src_ / "two/same.txt").string()}, dst_.string(), o).run();
  EXPECT_TRUE(r.ok());
  EXPECT_EQ(slurp(dst_ / "same.txt"), d1);
  EXPECT_EQ(slurp(dst_ / "same (2).txt"), d2);
}

TEST_F(FmTransfer, CancelWhileFilesAreWaitingForTheirCheckLeavesNothingBehind) {
  for (int i = 0; i < 200; ++i) random_file(src_ / "many" / ("f" + std::to_string(i)), 200000, static_cast<unsigned>(i));
  TransferOptions o;
  o.batch_files = 1000;  // nothing is checked until the end
  Transfer t({(src_ / "many").string()}, dst_.string(), o);
  std::thread canceller([&] {
    for (int i = 0; i < 4000 && t.progress().bytes_done < 5000000; ++i) usleep(500);
    t.cancel();
  });
  const TransferResult r = t.run();
  canceller.join();
  EXPECT_TRUE(r.cancelled);
  size_t files = 0;
  for (const auto& e : fs::recursive_directory_iterator(dst_))
    if (e.is_regular_file()) ++files;
  EXPECT_EQ(files, 0u) << "no temporary or half-checked file stays";
}

TEST_F(FmTransfer, EveryAlgorithmVerifiesACopy) {
  const std::string data = random_file(src_ / "a.bin", 300000);
  for (HashAlgo a : {HashAlgo::Sha256, HashAlgo::Sha512, HashAlgo::Blake2b, HashAlgo::Sha1, HashAlgo::Md5, HashAlgo::Auto}) {
    fs::remove_all(dst_);
    fs::create_directories(dst_);
    TransferOptions o;
    o.algo = a;
    const TransferResult r = Transfer({(src_ / "a.bin").string()}, dst_.string(), o).run();
    ASSERT_TRUE(r.ok()) << hash_name(a);
    ASSERT_EQ(r.verified.size(), 1u);
    EXPECT_EQ(r.verified[0].checksum, hash_file((dst_ / "a.bin").string(), resolve_algo(a))) << hash_name(a);
  }
}

TEST_F(FmTransfer, TopLevelResultsNameWhereEachItemWent) {
  random_file(src_ / "a.txt", 10, 1);
  random_file(src_ / "d/inner.txt", 10, 2);
  random_file(dst_ / "a.txt", 5, 3);
  TransferOptions o;
  o.on_conflict = [](const ConflictInfo&) { return Conflict::KeepBoth; };
  const TransferResult r = Transfer({(src_ / "a.txt").string(), (src_ / "d").string()}, dst_.string(), o).run();
  ASSERT_EQ(r.top_level.size(), 2u) << "files inside a folder are not listed";
  EXPECT_EQ(r.top_level[0].second, (dst_ / "a (2).txt").string());
  EXPECT_EQ(r.top_level[1].second, (dst_ / "d").string());
}
