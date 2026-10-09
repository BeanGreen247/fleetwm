#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

#include "dir_listing.hpp"

using namespace fleetwm::fm;
namespace fs = std::filesystem;

namespace {
class FmListing : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = fs::temp_directory_path() / ("fm-listing-" + std::to_string(::getpid()) + "-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
    fs::create_directories(dir_);
  }
  void TearDown() override {
    std::error_code ec;
    fs::remove_all(dir_, ec);
  }
  void file(const std::string& name, size_t bytes = 0) {
    std::ofstream(dir_ / name) << std::string(bytes, 'x');
  }
  fs::path dir_;
};
}  // namespace

TEST_F(FmListing, ListsNamesKindsAndSizes) {
  file("a.txt", 10);
  fs::create_directory(dir_ / "sub");
  fs::create_symlink("a.txt", dir_ / "link");
  fs::create_symlink("sub", dir_ / "dirlink");
  DirListing l;
  ASSERT_TRUE(list_dir(dir_, {}, &l));
  ASSERT_EQ(l.entries.size(), 4u);
  for (const Entry& e : l.entries) {
    const std::string n(l.name(e));
    if (n == "a.txt") {
      EXPECT_EQ(e.kind, Kind::File);
      EXPECT_EQ(e.size, 10u);
    } else if (n == "sub") {
      EXPECT_EQ(e.kind, Kind::Dir);
      EXPECT_TRUE(l.is_dir(e));
    } else if (n == "link") {
      EXPECT_EQ(e.kind, Kind::Symlink);
      EXPECT_FALSE(l.is_dir(e));
    } else if (n == "dirlink") {
      EXPECT_EQ(e.kind, Kind::Symlink);
      EXPECT_TRUE(l.is_dir(e));
    } else {
      ADD_FAILURE() << n;
    }
  }
}

TEST_F(FmListing, HiddenFilesCanBeLeftOut) {
  file(".hidden");
  file("shown");
  DirListing l;
  ASSERT_TRUE(list_dir(dir_, {true, false}, &l));
  ASSERT_EQ(l.entries.size(), 1u);
  EXPECT_EQ(l.name(l.entries[0]), "shown");
  ASSERT_TRUE(list_dir(dir_, {true, true}, &l));
  EXPECT_EQ(l.entries.size(), 2u);
}

TEST_F(FmListing, MissingFolderReportsWhy) {
  DirListing l;
  std::string err;
  EXPECT_FALSE(list_dir(dir_ / "nope", {}, &l, &err));
  EXPECT_FALSE(err.empty());
}

TEST_F(FmListing, ReadsMoreEntriesThanOneBuffer) {
  for (int i = 0; i < 3000; ++i) file("file-with-a-fairly-long-name-" + std::to_string(i));
  DirListing l;
  ASSERT_TRUE(list_dir(dir_, {false, true}, &l));
  EXPECT_EQ(l.entries.size(), 3000u);
}

TEST_F(FmListing, NameSortIsNaturalAndFoldersComeFirst) {
  file("file10");
  file("file2");
  file("File1");
  fs::create_directory(dir_ / "zdir");
  DirListing l;
  ASSERT_TRUE(list_dir(dir_, {}, &l));
  sort_listing(&l, SortKey::Name);
  ASSERT_EQ(l.entries.size(), 4u);
  EXPECT_EQ(l.name(l.entries[0]), "zdir");
  EXPECT_EQ(l.name(l.entries[1]), "File1");
  EXPECT_EQ(l.name(l.entries[2]), "file2");
  EXPECT_EQ(l.name(l.entries[3]), "file10");
  sort_listing(&l, SortKey::Name, false);
  EXPECT_EQ(l.name(l.entries[0]), "zdir");  // folders stay on top
  EXPECT_EQ(l.name(l.entries[1]), "file10");
}

TEST_F(FmListing, SortBySizeAndType) {
  file("big.txt", 100);
  file("small.txt", 1);
  file("mid.bin", 10);
  DirListing l;
  ASSERT_TRUE(list_dir(dir_, {}, &l));
  sort_listing(&l, SortKey::Size);
  EXPECT_EQ(l.name(l.entries[0]), "small.txt");
  EXPECT_EQ(l.name(l.entries[2]), "big.txt");
  sort_listing(&l, SortKey::Size, false);
  EXPECT_EQ(l.name(l.entries[0]), "big.txt");
  sort_listing(&l, SortKey::Type);
  EXPECT_EQ(l.name(l.entries[0]), "mid.bin");
}

TEST(FmListingNames, ExtensionRules) {
  EXPECT_EQ(extension_of("a.TXT"), "txt");
  EXPECT_EQ(extension_of("archive.tar.gz"), "gz");
  EXPECT_EQ(extension_of(".bashrc"), "");
  EXPECT_EQ(extension_of("noext"), "");
  EXPECT_EQ(extension_of("trailing."), "");
}

TEST_F(FmListing, TypeToSelectFindsPrefixesCaseInsensitivelyAndWraps) {
  file("alpha");
  file("Beta");
  file("beta2");
  DirListing l;
  ASSERT_TRUE(list_dir(dir_, {}, &l));
  sort_listing(&l, SortKey::Name);
  EXPECT_EQ(find_prefix(l, "be"), 1);
  EXPECT_EQ(find_prefix(l, "be", 2), 2);
  EXPECT_EQ(find_prefix(l, "be", 3), 1);  // wrapped
  EXPECT_EQ(find_prefix(l, "zz"), -1);
  EXPECT_EQ(find_prefix(l, ""), -1);
}

TEST_F(FmListing, ListingWithoutStatLeavesSizesForLaterButKnowsKinds) {
  file("a.txt", 10);
  fs::create_directory(dir_ / "sub");
  fs::create_symlink("sub", dir_ / "dirlink");
  DirListing l;
  ASSERT_TRUE(list_dir(dir_, {false, true}, &l));
  ASSERT_EQ(l.entries.size(), 3u);
  for (const Entry& e : l.entries) {
    const std::string n(l.name(e));
    if (n == "a.txt") {
      EXPECT_FALSE(e.has_stat);
      EXPECT_EQ(e.kind, Kind::File);
      EXPECT_EQ(e.size, 0u);
    } else if (n == "sub") {
      EXPECT_FALSE(e.has_stat);
      EXPECT_EQ(e.kind, Kind::Dir) << "the kind comes from d_type";
    } else {
      EXPECT_TRUE(e.has_stat) << "a symlink has to be followed to know if it opens like a folder";
      EXPECT_TRUE(l.is_dir(e));
    }
  }
}

TEST_F(FmListing, StatEntryFillsOneEntryOnDemand) {
  file("a.txt", 10);
  DirListing l;
  ASSERT_TRUE(list_dir(dir_, {false, true}, &l));
  Entry e = l.entries[0];
  ASSERT_TRUE(stat_entry(dir_.string(), "a.txt", &e));
  EXPECT_TRUE(e.has_stat);
  EXPECT_EQ(e.size, 10u);
  EXPECT_GT(e.mtime, 0);
  EXPECT_FALSE(stat_entry(dir_.string(), "gone.txt", &e));
}
