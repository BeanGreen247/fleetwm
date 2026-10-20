#include "vfs.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace fleetwm::netfs {

Vfs::Vfs(std::unique_ptr<Backend> backend, VfsOptions opt) : be_(std::move(backend)), opt_(std::move(opt)) {}

Vfs::~Vfs() {
  for (auto& [h, w] : writes_)
    if (w.fd >= 0) ::close(w.fd);
}

int64_t Vfs::now() const {
  if (opt_.clock) return opt_.clock();
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string Vfs::parent_of(const std::string& p) {
  const size_t s = p.rfind('/');
  if (s == std::string::npos || s == 0) return "/";
  return p.substr(0, s);
}

std::string Vfs::name_of(const std::string& p) {
  const size_t s = p.rfind('/');
  return s == std::string::npos ? p : p.substr(s + 1);
}

int Vfs::fail(const std::string& what, const std::string& err, int code) {
  last_error_ = what + ": " + err;
  return -code;
}

void Vfs::forget(const std::string& path) {
  stats_.erase(path);
  listings_.erase(parent_of(path));
  listings_.erase(path);
  if (block_.path == path) block_ = Block{};
}

int Vfs::getattr(const std::string& path, NetStat* st) {
  if (path == "/") {
    *st = NetStat{};
    st->dir = true;
    return 0;
  }
  const int64_t t = now();
  auto it = stats_.find(path);
  if (it != stats_.end() && it->second.expires > t) {
    if (it->second.missing) return -ENOENT;
    *st = it->second.st;
    return 0;
  }
  // A fresh listing of the parent already knows every name in it.
  auto lit = listings_.find(parent_of(path));
  if (lit != listings_.end() && lit->second.expires > t) {
    const std::string n = name_of(path);
    for (const NetEntry& e : lit->second.entries)
      if (e.name == n) {
        *st = e.st;
        return 0;
      }
    stats_[path] = Cached{NetStat{}, true, t + opt_.miss_ttl_s};
    return -ENOENT;
  }
  ++stat_calls_;
  bool missing = false;
  std::string err;
  if (!be_->stat(path, st, &missing, &err)) {
    if (missing) {
      stats_[path] = Cached{NetStat{}, true, t + opt_.miss_ttl_s};
      return -ENOENT;
    }
    return fail("stat " + path, err);
  }
  stats_[path] = Cached{*st, false, t + opt_.stat_ttl_s};
  return 0;
}

int Vfs::readdir(const std::string& path, std::vector<NetEntry>* out) {
  const int64_t t = now();
  auto it = listings_.find(path);
  if (it != listings_.end() && it->second.expires > t) {
    *out = it->second.entries;
    return 0;
  }
  ++list_calls_;
  std::string err;
  std::vector<NetEntry> entries;
  if (!be_->list(path, &entries, &err)) return fail("list " + path, err);
  for (const NetEntry& e : entries) {
    const std::string full = (path == "/" ? "" : path) + "/" + e.name;
    stats_[full] = Cached{e.st, false, t + opt_.stat_ttl_s};
  }
  listings_[path] = Listing{entries, t + opt_.stat_ttl_s};
  *out = std::move(entries);
  return 0;
}

int64_t Vfs::read(const std::string& path, uint64_t off, void* buf, size_t len) {
  if (len == 0) return 0;
  char* dst = static_cast<char*>(buf);
  size_t done = 0;
  while (done < len) {
    const uint64_t pos = off + done;
    if (block_.path == path && pos >= block_.off && pos < block_.off + block_.data.size()) {
      const size_t from = static_cast<size_t>(pos - block_.off);
      const size_t n = std::min(len - done, block_.data.size() - from);
      std::memcpy(dst + done, block_.data.data() + from, n);
      done += n;
      continue;
    }
    if (block_.path == path && block_.at_end && pos >= block_.off + block_.data.size()) break;
    Block b;
    b.path = path;
    b.off = pos;
    b.data.resize(std::max(opt_.read_block, len - done));
    std::string err;
    const int64_t n = be_->read(path, pos, b.data.data(), b.data.size(), &err);
    if (n < 0) {
      if (done > 0) break;
      return fail("read " + path, err);
    }
    b.data.resize(static_cast<size_t>(n));
    b.at_end = static_cast<size_t>(n) < std::max(opt_.read_block, len - done);
    block_ = std::move(b);
    if (n == 0) break;
  }
  return static_cast<int64_t>(done);
}

int Vfs::fetch_to(const std::string& path, uint64_t size, int fd) {
  std::string chunk(opt_.read_block, '\0');
  uint64_t off = 0;
  while (off < size) {
    std::string err;
    const int64_t n = be_->read(path, off, chunk.data(), chunk.size(), &err);
    if (n < 0) return fail("fetch " + path, err);
    if (n == 0) break;
    if (::pwrite(fd, chunk.data(), static_cast<size_t>(n), static_cast<off_t>(off)) != n) return -EIO;
    off += static_cast<uint64_t>(n);
  }
  return 0;
}

int Vfs::open_write(const std::string& path, bool truncate, bool create, uint64_t* handle) {
  NetStat st;
  const int g = getattr(path, &st);
  if (g == 0 && st.dir) return -EISDIR;
  if (g != 0 && !(g == -ENOENT && create)) return g;
  std::string dir = opt_.temp_dir;
  if (dir.empty()) {
    const char* x = std::getenv("XDG_RUNTIME_DIR");
    dir = x && *x ? x : "/tmp";
  }
  std::string tmpl = dir + "/fleetwm-netfs-XXXXXX";
  const int fd = ::mkstemp(tmpl.data());
  if (fd < 0) return -errno;
  ::unlink(tmpl.c_str());  // lives as long as the descriptor
  WriteBuf w;
  w.path = path;
  w.fd = fd;
  if (g == 0 && !truncate && st.size > 0) {
    const int r = fetch_to(path, st.size, fd);
    if (r != 0) {
      ::close(fd);
      return r;
    }
  } else if (g == -ENOENT || truncate) {
    w.dirty = true;  // a new or emptied file must reach the server even if nothing is written
  }
  if (g == -ENOENT) {
    // libfuse asks for the attributes of a file right after creating it; answer "empty file here" instead of the remembered miss.
    NetStat fresh;
    fresh.mtime = std::time(nullptr);
    stats_[path] = Cached{fresh, false, now() + opt_.stat_ttl_s};
    listings_.erase(parent_of(path));
  }
  *handle = next_handle_++;
  writes_[*handle] = std::move(w);
  return 0;
}

int64_t Vfs::write(uint64_t handle, uint64_t off, const void* data, size_t len) {
  auto it = writes_.find(handle);
  if (it == writes_.end()) return -EBADF;
  const ssize_t n = ::pwrite(it->second.fd, data, len, static_cast<off_t>(off));
  if (n < 0) return -errno;
  it->second.dirty = true;
  return n;
}

int64_t Vfs::read_handle(uint64_t handle, uint64_t off, void* buf, size_t len) {
  auto it = writes_.find(handle);
  if (it == writes_.end()) return -EBADF;
  const ssize_t n = ::pread(it->second.fd, buf, len, static_cast<off_t>(off));
  return n < 0 ? -errno : n;
}

int Vfs::truncate_handle(uint64_t handle, uint64_t size) {
  auto it = writes_.find(handle);
  if (it == writes_.end()) return -EBADF;
  if (::ftruncate(it->second.fd, static_cast<off_t>(size)) != 0) return -errno;
  it->second.dirty = true;
  return 0;
}

int Vfs::upload(WriteBuf& w) {
  // The backend uploads from a path; the descriptor is unlinked, so go through /proc/self/fd.
  const std::string local = "/proc/self/fd/" + std::to_string(w.fd);
  std::string err;
  if (!be_->put(w.path, local, &err)) return fail("upload " + w.path, err);
  w.dirty = false;
  forget(w.path);
  return 0;
}

int Vfs::flush(uint64_t handle) {
  auto it = writes_.find(handle);
  if (it == writes_.end()) return 0;
  if (!it->second.dirty) return 0;
  return upload(it->second);
}

void Vfs::release(uint64_t handle) {
  auto it = writes_.find(handle);
  if (it == writes_.end()) return;
  if (it->second.fd >= 0) ::close(it->second.fd);
  writes_.erase(it);
}

int Vfs::truncate_path(const std::string& path, uint64_t size) {
  uint64_t h = 0;
  int r = open_write(path, size == 0, false, &h);
  if (r != 0) return r;
  r = truncate_handle(h, size);
  if (r == 0) r = flush(h);
  release(h);
  return r;
}

int Vfs::mkdir(const std::string& path) {
  std::string err;
  if (!be_->mkdir(path, &err)) return fail("mkdir " + path, err);
  forget(path);
  return 0;
}

int Vfs::unlink(const std::string& path) {
  std::string err;
  if (!be_->remove_file(path, &err)) return fail("remove " + path, err);
  forget(path);
  return 0;
}

int Vfs::rmdir(const std::string& path) {
  std::string err;
  if (!be_->remove_dir(path, &err)) return fail("remove folder " + path, err);
  forget(path);
  return 0;
}

int Vfs::rename(const std::string& from, const std::string& to) {
  std::string err;
  if (!be_->rename(from, to, &err)) return fail("rename " + from, err);
  forget(from);
  forget(to);
  return 0;
}

}  // namespace fleetwm::netfs
