#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <string>

#include "backend.hpp"
#include "parsers.hpp"
#include "vfs.hpp"

using namespace fleetwm::netfs;

TEST(NetfsParsers, MlsdFactsAndSkippedDirs) {
  const std::string text =
      "type=cdir;modify=20240102030405; .\r\n"
      "type=pdir;modify=20240102030405; ..\r\n"
      "type=dir;modify=20240102030405;unix.mode=0755; sub folder\r\n"
      "Type=file;Size=1234;Modify=20230615101112; a b.txt\r\n";
  const auto v = parse_mlsd(text);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0].name, "sub folder");
  EXPECT_TRUE(v[0].st.dir);
  EXPECT_EQ(v[0].st.mode, 0755u);
  EXPECT_EQ(v[1].name, "a b.txt");
  EXPECT_EQ(v[1].st.size, 1234u);
  EXPECT_EQ(v[1].st.mtime, 1686823872);  // 2023-06-15 10:11:12 UTC
}

TEST(NetfsParsers, UnixListWithYearTimeLinksAndSpaces) {
  const int64_t now = 1760000000;  // 2025-10-09
  const std::string text =
      "total 12\r\n"
      "drwxr-xr-x   2 ftp ftp     4096 Jan  5 12:30 My Docs\r\n"
      "-rw-r--r--   1 ftp ftp   987654 Nov 11  2019 old file.iso\r\n"
      "lrwxrwxrwx   1 ftp ftp        7 Oct  1 09:00 latest -> abc.txt\r\n"
      "-rw-------   1 ftp ftp        0 Dec 31 23:59 future.txt\r\n";
  const auto v = parse_unix_list(text, now);
  ASSERT_EQ(v.size(), 4u);
  EXPECT_EQ(v[0].name, "My Docs");
  EXPECT_TRUE(v[0].st.dir);
  EXPECT_EQ(v[0].st.mode, 0755u);
  EXPECT_EQ(v[1].name, "old file.iso");
  EXPECT_EQ(v[1].st.size, 987654u);
  EXPECT_EQ(v[2].name, "latest");
  EXPECT_EQ(v[3].st.mode, 0600u);
  // "Dec 31 23:59" seen in October is last year's, not next December's
  EXPECT_LT(v[3].st.mtime, now);
}

TEST(NetfsParsers, PropfindAnyNamespacePrefix) {
  const std::string xml =
      "<?xml version=\"1.0\"?><D:multistatus xmlns:D=\"DAV:\">"
      "<D:response><D:href>/dav/docs/</D:href><D:propstat><D:prop><D:resourcetype><D:collection/></D:resourcetype></D:prop></D:propstat></D:response>"
      "<D:response><D:href>/dav/docs/a%20b.txt</D:href><D:propstat><D:prop><D:resourcetype/>"
      "<D:getcontentlength>42</D:getcontentlength><D:getlastmodified>Sun, 06 Nov 1994 08:49:37 GMT</D:getlastmodified></D:prop></D:propstat></D:response>"
      "<D:response><D:href>http://host/dav/docs/Sub%26Dir/</D:href><D:propstat><D:prop><D:resourcetype><D:collection/></D:resourcetype>"
      "</D:prop></D:propstat></D:response></D:multistatus>";
  const auto v = parse_propfind(xml, "/dav/docs");
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0].name, "a b.txt");
  EXPECT_FALSE(v[0].st.dir);
  EXPECT_EQ(v[0].st.size, 42u);
  EXPECT_EQ(v[0].st.mtime, 784111777);
  EXPECT_EQ(v[1].name, "Sub&Dir");
  EXPECT_TRUE(v[1].st.dir);
}

TEST(NetfsParsers, PropfindSingleAndNoPrefix) {
  NetStat st;
  ASSERT_TRUE(parse_propfind_one(
      "<multistatus xmlns=\"DAV:\"><response><href>/f.bin</href><propstat><prop><getcontentlength>7</getcontentlength>"
      "<resourcetype/></prop></propstat></response></multistatus>",
      &st));
  EXPECT_EQ(st.size, 7u);
  EXPECT_FALSE(st.dir);
}

TEST(NetfsParsers, UrlEscapes) {
  EXPECT_EQ(url_decode("a%20b%2Fc%zz"), "a b/c%zz");
  EXPECT_EQ(url_encode_path("/a b/é.txt"), "/a%20b/%C3%A9.txt");
}

namespace {

// An in-memory server that counts the calls that reach it.
class FakeBackend : public Backend {
 public:
  struct File {
    bool dir = false;
    std::string data;
  };
  std::map<std::string, File> files{{"/", {true, ""}}};
  int stats = 0, lists = 0, reads = 0, puts = 0;
  bool fail_put = false;

  bool connect(std::string*) override { return true; }
  bool list(const std::string& path, std::vector<NetEntry>* out, std::string* err) override {
    ++lists;
    if (!files.count(path) || !files[path].dir) {
      *err = "no such folder";
      return false;
    }
    const std::string prefix = path == "/" ? "/" : path + "/";
    for (auto& [p, f] : files) {
      if (p.size() <= prefix.size() || p.compare(0, prefix.size(), prefix) != 0) continue;
      if (p.find('/', prefix.size()) != std::string::npos) continue;
      NetEntry e;
      e.name = p.substr(prefix.size());
      e.st.dir = f.dir;
      e.st.size = f.data.size();
      out->push_back(e);
    }
    return true;
  }
  bool stat(const std::string& path, NetStat* st, bool* missing, std::string* err) override {
    ++stats;
    auto it = files.find(path);
    if (it == files.end()) {
      *missing = true;
      *err = "Not found";
      return false;
    }
    st->dir = it->second.dir;
    st->size = it->second.data.size();
    return true;
  }
  int64_t read(const std::string& path, uint64_t off, void* buf, size_t len, std::string* err) override {
    ++reads;
    auto it = files.find(path);
    if (it == files.end()) {
      *err = "gone";
      return -1;
    }
    if (off >= it->second.data.size()) return 0;
    const size_t n = std::min(len, it->second.data.size() - static_cast<size_t>(off));
    std::memcpy(buf, it->second.data.data() + off, n);
    return static_cast<int64_t>(n);
  }
  bool put(const std::string& path, const std::string& local, std::string* err) override {
    ++puts;
    if (fail_put) {
      *err = "disk full";
      return false;
    }
    FILE* f = std::fopen(local.c_str(), "rb");
    if (!f) {
      *err = "no local";
      return false;
    }
    std::string d;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) d.append(buf, n);
    std::fclose(f);
    files[path] = File{false, d};
    return true;
  }
  bool mkdir(const std::string& path, std::string*) override {
    files[path] = File{true, ""};
    return true;
  }
  bool remove_file(const std::string& path, std::string*) override { return files.erase(path) > 0; }
  bool remove_dir(const std::string& path, std::string*) override { return files.erase(path) > 0; }
  bool rename(const std::string& from, const std::string& to, std::string*) override {
    auto it = files.find(from);
    if (it == files.end()) return false;
    files[to] = it->second;
    files.erase(from);
    return true;
  }
};

struct Rig {
  FakeBackend* fake = nullptr;
  int64_t clock = 100;
  std::unique_ptr<Vfs> vfs;
  Rig() {
    auto be = std::make_unique<FakeBackend>();
    fake = be.get();
    VfsOptions o;
    o.temp_dir = "/tmp";
    o.read_block = 16;
    o.clock = [this] { return clock; };
    vfs = std::make_unique<Vfs>(std::move(be), o);
  }
};

}  // namespace

TEST(NetfsVfs, ListingAnswersTheStatsOfItsEntries) {
  Rig r;
  r.fake->files["/a.txt"] = {false, "hello"};
  r.fake->files["/d"] = {true, ""};
  std::vector<NetEntry> v;
  ASSERT_EQ(r.vfs->readdir("/", &v), 0);
  EXPECT_EQ(v.size(), 2u);
  NetStat st;
  EXPECT_EQ(r.vfs->getattr("/a.txt", &st), 0);
  EXPECT_EQ(st.size, 5u);
  EXPECT_EQ(r.vfs->getattr("/d", &st), 0);
  EXPECT_TRUE(st.dir);
  EXPECT_EQ(r.fake->stats, 0);  // never asked the server one by one
  EXPECT_EQ(r.vfs->getattr("/nothing", &st), -ENOENT);
  EXPECT_EQ(r.fake->stats, 0);  // a name missing from a fresh listing is not looked up either
}

TEST(NetfsVfs, CacheExpires) {
  Rig r;
  r.fake->files["/a"] = {false, "x"};
  NetStat st;
  EXPECT_EQ(r.vfs->getattr("/a", &st), 0);
  EXPECT_EQ(r.vfs->getattr("/a", &st), 0);
  EXPECT_EQ(r.fake->stats, 1);
  r.clock += 6;
  EXPECT_EQ(r.vfs->getattr("/a", &st), 0);
  EXPECT_EQ(r.fake->stats, 2);
  EXPECT_EQ(r.vfs->getattr("/gone", &st), -ENOENT);
  EXPECT_EQ(r.vfs->getattr("/gone", &st), -ENOENT);
  EXPECT_EQ(r.fake->stats, 3);  // the miss was remembered
}

TEST(NetfsVfs, ReadsComeFromOneBlock) {
  Rig r;
  r.fake->files["/f"] = {false, "0123456789abcdefghijklmnopqrstuvwxyz"};  // 36 bytes, block 16
  char buf[8];
  EXPECT_EQ(r.vfs->read("/f", 0, buf, 4), 4);
  EXPECT_EQ(std::string(buf, 4), "0123");
  EXPECT_EQ(r.vfs->read("/f", 4, buf, 8), 8);
  EXPECT_EQ(std::string(buf, 8), "456789ab");
  EXPECT_EQ(r.fake->reads, 1);
  char big[40];
  EXPECT_EQ(r.vfs->read("/f", 30, big, 40), 6);
  EXPECT_EQ(std::string(big, 6), "uvwxyz");
  EXPECT_EQ(r.vfs->read("/f", 36, big, 10), 0);
}

TEST(NetfsVfs, WriteIsUploadedOnFlushAndReportsFailure) {
  Rig r;
  uint64_t h = 0;
  ASSERT_EQ(r.vfs->open_write("/new.txt", true, true, &h), 0);
  EXPECT_EQ(r.vfs->write(h, 0, "hello ", 6), 6);
  EXPECT_EQ(r.vfs->write(h, 6, "world", 5), 5);
  EXPECT_EQ(r.fake->puts, 0);
  ASSERT_EQ(r.vfs->flush(h), 0);
  EXPECT_EQ(r.fake->files["/new.txt"].data, "hello world");
  EXPECT_EQ(r.vfs->flush(h), 0);
  EXPECT_EQ(r.fake->puts, 1);  // nothing changed since
  r.vfs->release(h);

  r.fake->fail_put = true;
  ASSERT_EQ(r.vfs->open_write("/bad.txt", true, true, &h), 0);
  r.vfs->write(h, 0, "x", 1);
  EXPECT_EQ(r.vfs->flush(h), -EIO);
  EXPECT_NE(r.vfs->last_error().find("disk full"), std::string::npos);
  r.vfs->release(h);
}

TEST(NetfsVfs, EmptyNewFileStillReachesTheServer) {
  Rig r;
  uint64_t h = 0;
  ASSERT_EQ(r.vfs->open_write("/empty", true, true, &h), 0);
  ASSERT_EQ(r.vfs->flush(h), 0);
  r.vfs->release(h);
  ASSERT_TRUE(r.fake->files.count("/empty"));
  EXPECT_EQ(r.fake->files["/empty"].data, "");
}

TEST(NetfsVfs, AppendFetchesTheOldContentFirst) {
  Rig r;
  r.fake->files["/log"] = {false, "line1\n"};
  uint64_t h = 0;
  ASSERT_EQ(r.vfs->open_write("/log", false, false, &h), 0);
  EXPECT_EQ(r.vfs->write(h, 6, "line2\n", 6), 6);
  char buf[16];
  EXPECT_EQ(r.vfs->read_handle(h, 0, buf, 12), 12);
  ASSERT_EQ(r.vfs->flush(h), 0);
  r.vfs->release(h);
  EXPECT_EQ(r.fake->files["/log"].data, "line1\nline2\n");
}

TEST(NetfsVfs, TruncateAndOperationsForgetCaches) {
  Rig r;
  r.fake->files["/t"] = {false, "abcdef"};
  ASSERT_EQ(r.vfs->truncate_path("/t", 3), 0);
  EXPECT_EQ(r.fake->files["/t"].data, "abc");
  NetStat st;
  ASSERT_EQ(r.vfs->mkdir("/m"), 0);
  EXPECT_EQ(r.vfs->getattr("/m", &st), 0);
  EXPECT_TRUE(st.dir);
  ASSERT_EQ(r.vfs->rename("/t", "/m/t2"), 0);
  EXPECT_EQ(r.vfs->getattr("/t", &st), -ENOENT);
  EXPECT_EQ(r.vfs->getattr("/m/t2", &st), 0);
  ASSERT_EQ(r.vfs->unlink("/m/t2"), 0);
  EXPECT_EQ(r.vfs->getattr("/m/t2", &st), -ENOENT);
  EXPECT_EQ(r.vfs->open_write("/m", false, false, new uint64_t), -EISDIR);
}

TEST(NetfsVfs, CreatingAFileOverridesTheRememberedMiss) {
  Rig r;
  NetStat st;
  EXPECT_EQ(r.vfs->getattr("/n", &st), -ENOENT);  // the kernel looks first and the miss is cached
  uint64_t h = 0;
  ASSERT_EQ(r.vfs->open_write("/n", true, true, &h), 0);
  EXPECT_EQ(r.vfs->getattr("/n", &st), 0);        // libfuse stats the new file right after create
  EXPECT_FALSE(st.dir);
  EXPECT_EQ(st.size, 0u);
  r.vfs->release(h);
}
