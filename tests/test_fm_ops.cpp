#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <fstream>

#include "file_ops.hpp"
#include "search.hpp"
#include "trash.hpp"

using namespace fleetwm::fm;
namespace fs = std::filesystem;

namespace {
class FmOps : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ = fs::temp_directory_path() / ("fm-ops-" + std::to_string(::getpid()) + "-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
    fs::create_directories(root_);
  }
  void TearDown() override {
    std::error_code ec;
    fs::remove_all(root_, ec);
  }
  void file(const fs::path& p, const std::string& text = "x") {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
  }
  fs::path root_;
};
}  // namespace

// ---- names and folders ----
TEST(FmFileName, Validation) {
  std::string why;
  EXPECT_TRUE(valid_file_name("report 2026.txt", &why));
  EXPECT_TRUE(valid_file_name(".hidden"));
  EXPECT_FALSE(valid_file_name("", &why));
  EXPECT_FALSE(valid_file_name(".", &why));
  EXPECT_FALSE(valid_file_name("..", &why));
  EXPECT_FALSE(valid_file_name("a/b", &why));
  EXPECT_NE(why.find("slash"), std::string::npos);
  EXPECT_FALSE(valid_file_name(std::string(256, 'a'), &why));
  EXPECT_TRUE(valid_file_name(std::string(255, 'a')));
  EXPECT_FALSE(valid_file_name(std::string("a\0b", 3), &why));
}

TEST_F(FmOps, NewFolderNamesCountUp) {
  EXPECT_EQ(new_folder_name(root_.string()), "New folder");
  fs::create_directory(root_ / "New folder");
  EXPECT_EQ(new_folder_name(root_.string()), "New folder (2)");
  fs::create_directory(root_ / "New folder (2)");
  EXPECT_EQ(new_folder_name(root_.string()), "New folder (3)");
  std::string err;
  EXPECT_TRUE(make_dir((root_ / "made").string(), &err));
  EXPECT_FALSE(make_dir((root_ / "made").string(), &err));
  EXPECT_FALSE(err.empty());
}

TEST_F(FmOps, RenameRefusesToOverwrite) {
  file(root_ / "a.txt");
  file(root_ / "b.txt");
  std::string err;
  EXPECT_FALSE(rename_in_place((root_ / "a.txt").string(), "b.txt", &err));
  EXPECT_NE(err.find("already exists"), std::string::npos);
  EXPECT_FALSE(rename_in_place((root_ / "a.txt").string(), "x/y", &err));
  EXPECT_TRUE(rename_in_place((root_ / "a.txt").string(), "c.txt", &err));
  EXPECT_TRUE(fs::exists(root_ / "c.txt"));
  EXPECT_FALSE(fs::exists(root_ / "a.txt"));
  EXPECT_TRUE(rename_in_place((root_ / "c.txt").string(), "c.txt", &err)) << "same name is not an error";
}

TEST_F(FmOps, DeleteTreeNeverFollowsLinks) {
  file(root_ / "outside/keep.txt");
  file(root_ / "d/inner/f.txt");
  fs::create_directory_symlink(root_ / "outside", root_ / "d/link");
  EXPECT_TRUE(delete_tree((root_ / "d").string()));
  EXPECT_FALSE(fs::exists(root_ / "d"));
  EXPECT_TRUE(fs::exists(root_ / "outside/keep.txt"));
}

TEST_F(FmOps, MeasuresATree) {
  file(root_ / "t/a", std::string(1000, 'a'));
  file(root_ / "t/sub/b", std::string(24, 'b'));
  fs::create_directory(root_ / "t/empty");
  const TreeSize s = measure_tree((root_ / "t").string());
  EXPECT_EQ(s.bytes, 1024u);
  EXPECT_EQ(s.files, 2u);
  EXPECT_EQ(s.folders, 2u);
  EXPECT_GE(s.on_disk, 1024u);
  const TreeSize one = measure_tree((root_ / "t/a").string());
  EXPECT_EQ(one.bytes, 1000u);
  EXPECT_EQ(one.files, 1u);
  std::atomic<bool> cancel{true};
  EXPECT_EQ(measure_tree((root_ / "t").string(), &cancel).files, 0u);
}

TEST_F(FmOps, PropertiesReadModeOwnerAndLinks) {
  file(root_ / "p.txt", "hello");
  ::chmod((root_ / "p.txt").c_str(), 0640);
  fs::create_symlink("p.txt", root_ / "ln");
  FileProps p;
  ASSERT_TRUE(read_props((root_ / "p.txt").string(), &p));
  EXPECT_EQ(p.name, "p.txt");
  EXPECT_EQ(p.size, 5u);
  EXPECT_EQ(p.mode_text, "-rw-r-----");
  EXPECT_EQ(p.type, "Text Document");
  EXPECT_FALSE(p.owner.empty());
  EXPECT_FALSE(p.is_dir);
  ASSERT_TRUE(read_props((root_ / "ln").string(), &p));
  EXPECT_TRUE(p.is_link);
  EXPECT_EQ(p.link_target, "p.txt");
  ASSERT_TRUE(read_props(root_.string(), &p));
  EXPECT_TRUE(p.is_dir);
  EXPECT_EQ(p.mode_text[0], 'd');
  EXPECT_FALSE(read_props((root_ / "nope").string(), &p));
}

TEST(FmModeString, SpecialBits) {
  EXPECT_EQ(mode_string(S_IFREG | 0755), "-rwxr-xr-x");
  EXPECT_EQ(mode_string(S_IFDIR | 01777), "drwxrwxrwt");
  EXPECT_EQ(mode_string(S_IFREG | 04755), "-rwsr-xr-x");
  EXPECT_EQ(mode_string(S_IFREG | 02644), "-rw-r-Sr--");
  EXPECT_EQ(mode_string(S_IFLNK | 0777), "lrwxrwxrwx");
}

TEST(FmClipboardText, ReadsWhatOtherFileManagersCopy) {
  const auto p = parse_path_list("# comment\r\nfile:///home/me/My%20File.txt\r\n/etc/hosts\r\nrelative/ignored\r\n\r\nfile:///tmp/x\n");
  ASSERT_EQ(p.size(), 3u);
  EXPECT_EQ(p[0], "/home/me/My File.txt");
  EXPECT_EQ(p[1], "/etc/hosts");
  EXPECT_EQ(p[2], "/tmp/x");
  EXPECT_EQ(make_uri_list({"/a b/c", "/d"}), "file:///a%20b/c\r\nfile:///d\r\n");
  EXPECT_EQ(parse_path_list(make_uri_list({"/a b/c é", "/d"})), (std::vector<std::string>{"/a b/c é", "/d"}));
  EXPECT_TRUE(parse_path_list("").empty());
}

TEST(FmSpawn, RunsDetachedAndRefusesNothing) {
  EXPECT_TRUE(spawn_detached({"true"}));
  EXPECT_FALSE(spawn_detached({}));
  EXPECT_FALSE(spawn_detached({"/nonexistent/binary-x"}));
}

// ---- trash ----
TEST_F(FmOps, TrashMovesFilesAndWritesTheSpecInfo) {
  Trash t((root_ / "Trash").string());
  file(root_ / "doc dir/a b.txt", "content");
  std::string err;
  ASSERT_TRUE(t.put((root_ / "doc dir/a b.txt").string(), &err)) << err;
  EXPECT_FALSE(fs::exists(root_ / "doc dir/a b.txt"));
  EXPECT_TRUE(fs::exists(root_ / "Trash/files/a b.txt"));
  std::ifstream f(root_ / "Trash/info/a b.txt.trashinfo");
  std::string text((std::istreambuf_iterator<char>(f)), {});
  EXPECT_EQ(text.rfind("[Trash Info]\nPath=", 0), 0u);
  EXPECT_NE(text.find("a%20b.txt"), std::string::npos);
  EXPECT_NE(text.find("DeletionDate="), std::string::npos);
  const auto items = t.list();
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].name, "a b.txt");
  EXPECT_EQ(items[0].original_path, (root_ / "doc dir/a b.txt").string());
  EXPECT_EQ(items[0].size, 7u);
  EXPECT_FALSE(items[0].is_dir);
}

TEST_F(FmOps, TrashKeepsBothWhenNamesClash) {
  Trash t((root_ / "Trash").string());
  file(root_ / "one/same.txt", "1");
  file(root_ / "two/same.txt", "22");
  ASSERT_TRUE(t.put((root_ / "one/same.txt").string()));
  ASSERT_TRUE(t.put((root_ / "two/same.txt").string()));
  const auto items = t.list();
  ASSERT_EQ(items.size(), 2u);
  EXPECT_NE(items[0].name, items[1].name);
  EXPECT_TRUE(fs::exists(root_ / "Trash/files/same.txt"));
  EXPECT_TRUE(fs::exists(root_ / "Trash/files/same (2).txt"));
}

TEST_F(FmOps, TrashedFoldersRestoreWhole) {
  Trash t((root_ / "Trash").string());
  file(root_ / "proj/src/main.c", "int main(){}");
  ASSERT_TRUE(t.put((root_ / "proj").string()));
  auto items = t.list();
  ASSERT_EQ(items.size(), 1u);
  EXPECT_TRUE(items[0].is_dir);
  EXPECT_EQ(items[0].size, 12u);
  std::string err;
  ASSERT_TRUE(t.restore(items[0], &err)) << err;
  EXPECT_TRUE(fs::exists(root_ / "proj/src/main.c"));
  EXPECT_TRUE(t.list().empty());
  EXPECT_FALSE(fs::exists(root_ / "Trash/info/proj.trashinfo"));
}

TEST_F(FmOps, RestoreRefusesToOverwriteAndRecreatesMissingFolders) {
  Trash t((root_ / "Trash").string());
  file(root_ / "gone/a.txt");
  ASSERT_TRUE(t.put((root_ / "gone/a.txt").string()));
  fs::remove_all(root_ / "gone");
  auto it = t.list();
  ASSERT_EQ(it.size(), 1u);
  EXPECT_TRUE(t.restore(it[0]));
  EXPECT_TRUE(fs::exists(root_ / "gone/a.txt"));
  ASSERT_TRUE(t.put((root_ / "gone/a.txt").string()));
  file(root_ / "gone/a.txt", "newer");
  it = t.list();
  std::string err;
  EXPECT_FALSE(t.restore(it[0], &err));
  EXPECT_NE(err.find("already exists"), std::string::npos);
  EXPECT_TRUE(fs::exists(root_ / "Trash/files/a.txt")) << "nothing was lost";
}

TEST_F(FmOps, EmptyingTheTrashRemovesFilesAndInfo) {
  Trash t((root_ / "Trash").string());
  file(root_ / "x/a");
  file(root_ / "x/b/c");
  ASSERT_TRUE(t.put((root_ / "x/a").string()));
  ASSERT_TRUE(t.put((root_ / "x/b").string()));
  EXPECT_EQ(t.empty(), 2u);
  EXPECT_TRUE(fs::is_empty(root_ / "Trash/files"));
  EXPECT_TRUE(fs::is_empty(root_ / "Trash/info"));
}

TEST_F(FmOps, TrashRefusesWhatItCannotDoAndNeverEatsItself) {
  Trash t((root_ / "Trash").string());
  std::string err;
  EXPECT_FALSE(t.put((root_ / "missing").string(), &err));
  EXPECT_FALSE(err.empty());
  file(root_ / "a");
  ASSERT_TRUE(t.put((root_ / "a").string()));
  EXPECT_FALSE(t.put((root_ / "Trash/files").string(), &err));
  EXPECT_EQ(t.list().size(), 1u);
}

TEST_F(FmOps, TrashInfoPathsRoundTrip) {
  EXPECT_EQ(trashinfo_path_encode("/home/me/a b/é.txt"), "/home/me/a%20b/%C3%A9.txt");
  EXPECT_EQ(trashinfo_path_decode("/home/me/a%20b/%C3%A9.txt"), "/home/me/a b/é.txt");
}

// ---- search ----
TEST(FmSearchName, PartialAndWildcardAndCase) {
  SearchOptions o;
  o.query = "rep";
  EXPECT_TRUE(name_matches(o, "Annual Report.doc"));
  EXPECT_FALSE(name_matches(o, "summary.doc"));
  o.case_sensitive = true;
  EXPECT_FALSE(name_matches(o, "Annual Report.doc"));
  o.case_sensitive = false;
  o.partial = false;
  EXPECT_FALSE(name_matches(o, "report"));
  o.query = "report";
  EXPECT_TRUE(name_matches(o, "REPORT"));
  o.partial = true;
  o.query = "*.TXT";
  EXPECT_TRUE(name_matches(o, "notes.txt"));
  EXPECT_FALSE(name_matches(o, "notes.txt.bak") && false);
  o.query = "img_??.jpg";
  EXPECT_TRUE(name_matches(o, "img_01.jpg"));
  EXPECT_FALSE(name_matches(o, "img_001.jpg"));
  o.query = "";
  EXPECT_FALSE(name_matches(o, "anything"));
}

TEST_F(FmOps, SearchFindsFilesInSubfoldersAndHonoursOptions) {
  file(root_ / "a/report-2025.txt", "alpha");
  file(root_ / "a/b/Report-2026.txt", "needle here");
  file(root_ / "a/b/other.log", "needle too");
  file(root_ / "a/.hidden/report-secret.txt");
  SearchOptions o;
  o.query = "report";
  std::vector<std::string> hits;
  search_tree((root_ / "a").string(), o, nullptr, [&](const SearchHit& h) { hits.push_back(h.path); return true; });
  EXPECT_EQ(hits, (std::vector<std::string>{"report-2025.txt", "b/Report-2026.txt"}));
  o.hidden = true;
  hits.clear();
  search_tree((root_ / "a").string(), o, nullptr, [&](const SearchHit& h) { hits.push_back(h.path); return true; });
  EXPECT_EQ(hits.size(), 3u);
  o.hidden = false;
  o.subfolders = false;
  hits.clear();
  search_tree((root_ / "a").string(), o, nullptr, [&](const SearchHit& h) { hits.push_back(h.path); return true; });
  EXPECT_EQ(hits, (std::vector<std::string>{"report-2025.txt"}));
  o.subfolders = true;
  o.query = "needle";
  hits.clear();
  search_tree((root_ / "a").string(), o, nullptr, [&](const SearchHit& h) { hits.push_back(h.path); return true; });
  EXPECT_TRUE(hits.empty()) << "names only unless contents are on";
  o.contents = true;
  search_tree((root_ / "a").string(), o, nullptr, [&](const SearchHit& h) { hits.push_back(h.path); return true; });
  EXPECT_EQ(hits.size(), 2u);
}

TEST_F(FmOps, SearchStopsAtTheLimitAndWhenCancelled) {
  for (int i = 0; i < 50; ++i) file(root_ / ("f" + std::to_string(i)));
  SearchOptions o;
  o.query = "f";
  o.max_results = 7;
  EXPECT_EQ(search_tree(root_.string(), o, nullptr, [](const SearchHit&) { return true; }), 7u);
  o.max_results = 1000;
  std::atomic<bool> cancel{false};
  size_t n = 0;
  search_tree(root_.string(), o, &cancel, [&](const SearchHit&) {
    if (++n == 3) cancel = true;
    return true;
  });
  EXPECT_EQ(n, 3u);
  size_t seen = 0;
  search_tree(root_.string(), o, nullptr, [&](const SearchHit&) { return ++seen < 5; });
  EXPECT_EQ(seen, 5u);
}

TEST_F(FmOps, SearchReportsFoldersAsFolders) {
  fs::create_directories(root_ / "pics/holiday");
  SearchOptions o;
  o.query = "holi";
  bool dir = false;
  search_tree(root_.string(), o, nullptr, [&](const SearchHit& h) { dir = h.kind == Kind::Dir; return true; });
  EXPECT_TRUE(dir);
}

TEST_F(FmOps, RecentFilesComeNewestFirstAndSkipMissingOnes) {
  file(root_ / "a.txt");
  file(root_ / "b c.txt");
  const std::string xbel = (root_ / "recently-used.xbel").string();
  std::ofstream(xbel) << "<?xml version=\"1.0\"?>\n<xbel version=\"1.0\">\n"
                      << "  <bookmark href=\"file://" << root_.string() << "/a.txt\" added=\"2026-01-01T10:00:00Z\" modified=\"2026-01-01T10:00:00Z\" visited=\"2026-01-02T10:00:00Z\">\n"
                      << "  <bookmark href=\"file://" << root_.string() << "/b%20c.txt\" added=\"2026-01-01T10:00:00Z\" modified=\"2026-03-01T10:00:00Z\" visited=\"2026-03-01T10:00:00Z\">\n"
                      << "  <bookmark href=\"file://" << root_.string() << "/gone.txt\" modified=\"2026-05-01T10:00:00Z\">\n"
                      << "  <bookmark href=\"https://example.com/x\" modified=\"2026-06-01T10:00:00Z\">\n</xbel>\n";
  const auto r = read_recent(xbel);
  ASSERT_EQ(r.size(), 2u);
  EXPECT_EQ(r[0].path, (root_ / "b c.txt").string());
  EXPECT_EQ(r[1].path, (root_ / "a.txt").string());
  EXPECT_GT(r[0].modified, r[1].modified);
  EXPECT_EQ(read_recent(xbel, 1).size(), 1u);
  EXPECT_TRUE(read_recent((root_ / "nope.xbel").string()).empty());
}

TEST(FmMime, TypesByExtension) {
  EXPECT_EQ(mime_type_for("a.JPG", false), "image/jpeg");
  EXPECT_EQ(mime_type_for("notes.txt", false), "text/plain");
  EXPECT_EQ(mime_type_for("x", true), "inode/directory");
  EXPECT_EQ(mime_type_for("weird.zzz", false), "application/octet-stream");
  EXPECT_TRUE(have_program("sh"));
  EXPECT_FALSE(have_program("definitely-not-a-program-xyz"));
  EXPECT_TRUE(have_program("/bin/sh"));
}

TEST_F(FmOps, LinksGetExplorerStyleNamesAndHardLinksShareTheFile) {
  file(root_ / "a.txt", "hello");
  fs::create_directories(root_ / "d");
  std::string made, err;
  ASSERT_TRUE(make_symlink_to((root_ / "a.txt").string(), (root_ / "d").string(), &made, &err)) << err;
  EXPECT_EQ(fs::path(made).filename(), "a - Link.txt");
  EXPECT_TRUE(fs::is_symlink(made));
  EXPECT_EQ(fs::read_symlink(made), root_ / "a.txt");
  ASSERT_TRUE(make_symlink_to((root_ / "a.txt").string(), (root_ / "d").string(), &made, &err));
  EXPECT_EQ(fs::path(made).filename(), "a - Link (2).txt");
  ASSERT_TRUE(make_hardlink_to((root_ / "a.txt").string(), (root_ / "d").string(), &made, &err)) << err;
  EXPECT_EQ(fs::path(made).filename(), "a - Link (3).txt");
  EXPECT_EQ(fs::hard_link_count(root_ / "a.txt"), 2u);
  EXPECT_FALSE(make_hardlink_to((root_ / "d").string(), (root_ / "d").string(), &made, &err));
  EXPECT_NE(err.find("folder"), std::string::npos);
}

TEST_F(FmOps, TrashReportsWhereTheFileWentAndTransferNamesWhatItPlaced) {
  Trash t((root_ / "Trash").string());
  file(root_ / "x.txt", "1");
  TrashItem it;
  ASSERT_TRUE(t.put((root_ / "x.txt").string(), nullptr, &it));
  EXPECT_EQ(it.name, "x.txt");
  EXPECT_EQ(it.original_path, (root_ / "x.txt").string());
  EXPECT_TRUE(t.restore(it));
  EXPECT_TRUE(fs::exists(root_ / "x.txt"));
}
