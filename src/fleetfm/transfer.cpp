#include "transfer.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>

#include "dir_listing.hpp"

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif

namespace fleetwm::fm {

namespace {

double now_s() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string base_of(const std::string& p) {
  size_t end = p.size();
  while (end > 1 && p[end - 1] == '/') --end;
  const size_t slash = p.rfind('/', end - 1);
  return p.substr(slash == std::string::npos ? 0 : slash + 1, end - (slash == std::string::npos ? 0 : slash + 1));
}

std::string join(const std::string& a, const std::string& b) { return a.empty() || a.back() == '/' ? a + b : a + "/" + b; }

bool exists(const std::string& p) {
  struct stat st;
  return ::lstat(p.c_str(), &st) == 0;
}

// Two paths are the same file (hard link or same path) when device and inode match.
bool same_file(const std::string& a, const std::string& b) {
  struct stat sa, sb;
  return ::stat(a.c_str(), &sa) == 0 && ::stat(b.c_str(), &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

bool write_all(int fd, const char* p, size_t n) {
  while (n) {
    const ssize_t w = ::write(fd, p, n);
    if (w < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    p += w;
    n -= static_cast<size_t>(w);
  }
  return true;
}

struct AlignedBuf {
  void* p = nullptr;
  explicit AlignedBuf(size_t n) {
    if (posix_memalign(&p, 4096, n) != 0) p = nullptr;
  }
  ~AlignedBuf() { std::free(p); }
  char* data() const { return static_cast<char*>(p); }
};

}  // namespace

double Progress::fraction() const {
  const uint64_t total = bytes_total + verify_total;
  if (total == 0) return phase == Phase::Done ? 1.0 : 0.0;
  return std::min(1.0, static_cast<double>(bytes_done + verified) / static_cast<double>(total));
}

std::string unique_name(const std::string& dir, const std::string& name) {
  if (!exists(join(dir, name))) return name;
  const size_t dot = name.rfind('.');
  const bool has_ext = dot != std::string::npos && dot != 0;
  const std::string stem = has_ext ? name.substr(0, dot) : name, ext = has_ext ? name.substr(dot) : "";
  for (int i = 2;; ++i) {
    std::string cand = stem + " (" + std::to_string(i) + ")" + ext;
    if (!exists(join(dir, cand))) return cand;
  }
}

std::string copy_name(const std::string& dir, const std::string& name) {
  const size_t dot = name.rfind('.');
  const bool has_ext = dot != std::string::npos && dot != 0;
  const std::string stem = has_ext ? name.substr(0, dot) : name, ext = has_ext ? name.substr(dot) : "";
  std::string cand = stem + " - Copy" + ext;
  for (int i = 2; exists(join(dir, cand)); ++i) cand = stem + " - Copy (" + std::to_string(i) + ")" + ext;
  return cand;
}

Transfer::Transfer(std::vector<std::string> sources, std::string dest_dir, TransferOptions opt)
    : sources_(std::move(sources)), dest_dir_(std::move(dest_dir)), opt_(std::move(opt)) {
  if (opt_.block_bytes < 64 * 1024) opt_.block_bytes = 64 * 1024;
  opt_.block_bytes = (opt_.block_bytes + 4095) & ~size_t(4095);
  opt_.algo = resolve_algo(opt_.algo);
  if (posix_memalign(&io_buf_, 4096, opt_.block_bytes) != 0) io_buf_ = nullptr;
}

Transfer::~Transfer() { std::free(io_buf_); }

bool Transfer::name_is_pending(const std::string& final_dst) const {
  for (const Pending& p : pending_)
    if (p.final_dst == final_dst) return true;
  return false;
}

Progress Transfer::progress() const {
  std::lock_guard<std::mutex> l(mu_);
  return prog_;
}

void Transfer::cancel() {
  cancel_ = true;
  std::lock_guard<std::mutex> l(mu_);
  paused_ = false;
  cv_.notify_all();
}

void Transfer::pause(bool on) {
  std::lock_guard<std::mutex> l(mu_);
  paused_ = on;
  prog_.paused = on;
  cv_.notify_all();
}

bool Transfer::wait_if_paused() {
  std::unique_lock<std::mutex> l(mu_);
  cv_.wait(l, [&] { return !paused_ || cancel_; });
  return !cancel_;
}

void Transfer::note(uint64_t copied, uint64_t verified) {
  std::lock_guard<std::mutex> l(mu_);
  prog_.bytes_done += copied;
  prog_.verified += verified;
  const double t = now_s();
  meter_.update(t - t0_, prog_.bytes_done + prog_.verified);
  prog_.speed = meter_.bytes_per_second();
  const uint64_t total = prog_.bytes_total + prog_.verify_total, done = prog_.bytes_done + prog_.verified;
  prog_.eta = meter_.eta_seconds(total > done ? total - done : 0);
}

void Transfer::fail(const std::string& path, const std::string& msg) { result_.errors.push_back({path, msg}); }

bool Transfer::add_tree(const std::string& src, const std::string& dst, std::vector<Item>* plan) {
  struct stat st;
  if (::lstat(src.c_str(), &st) != 0) {
    fail(src, std::strerror(errno));
    return true;
  }
  if (S_ISLNK(st.st_mode) && opt_.follow_symlinks) {
    if (::stat(src.c_str(), &st) != 0) {
      fail(src, std::strerror(errno));
      return true;
    }
  }
  Item it{src, dst, 0, static_cast<uint64_t>(st.st_size), st.st_mode & 07777u, st.st_mtim.tv_sec, st.st_dev};
  if (S_ISDIR(st.st_mode)) {
    it.type = 1;
    it.size = 0;
    plan->push_back(it);
    DirListing l;
    std::string err;
    if (!list_dir(src, {false, true}, &l, &err)) {
      fail(src, err);
      return true;
    }
    for (const Entry& e : l.entries) {
      if (cancel_) return false;
      const std::string name(l.name(e));
      if (!add_tree(join(src, name), join(dst, name), plan)) return false;
    }
  } else if (S_ISLNK(st.st_mode)) {
    it.type = 2;
    it.size = 0;
    plan->push_back(it);
  } else if (S_ISREG(st.st_mode)) {
    plan->push_back(it);
  } else {
    fail(src, "Not a regular file, folder or link");
  }
  return true;
}

bool Transfer::discover() {
  {
    std::lock_guard<std::mutex> l(mu_);
    prog_.phase = Phase::Discovering;
  }
  for (const std::string& s : sources_) {
    if (cancel_) return false;
    const std::string name = base_of(s);
    const std::string dst = join(dest_dir_, name);
    // A folder cannot go into itself or below itself.
    std::error_code ec;
    const auto cs = std::filesystem::weakly_canonical(s, ec), cd = std::filesystem::weakly_canonical(dest_dir_, ec);
    struct stat st;
    if (::lstat(s.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
      const std::string a = cs.string() + "/", b = cd.string() + "/";
      if (b.compare(0, a.size(), a) == 0) {
        fail(s, "The destination folder is inside the source folder");
        continue;
      }
    }
    if (opt_.move && same_file(s, dst)) continue;
    if (opt_.move && !exists(dst) && ::renameat2(AT_FDCWD, s.c_str(), AT_FDCWD, dst.c_str(), RENAME_NOREPLACE) == 0) {
      ++result_.files_copied;  // same file system: a rename, nothing to copy or check
      result_.top_level.emplace_back(s, dst);
      continue;
    }
    if (!add_tree(s, dst, &plan_)) return false;
  }
  uint64_t bytes = 0, files = 0;
  for (const Item& it : plan_) {
    if (it.type != 1) ++files;
    bytes += it.size;
  }
  std::lock_guard<std::mutex> l(mu_);
  prog_.bytes_total = bytes;
  prog_.verify_total = opt_.verify ? bytes : 0;
  prog_.files_total = files;
  return true;
}

// Decides where an item that already has something at its destination goes. Folders merge.
bool Transfer::resolve_conflict(const Item& it, std::string* dst, bool* skip) {
  *skip = false;
  if (!pending_.empty() && name_is_pending(*dst) && !flush_batch()) {  // a file with this name is written but not renamed yet
    *skip = true;
    return false;
  }
  struct stat ds;
  if (::lstat(dst->c_str(), &ds) != 0) return true;
  if (same_file(it.src, *dst)) {
    if (opt_.move) {
      *skip = true;  // moving a file onto itself: nothing to do
      return true;
    }
    // Copying a file or folder into the folder it is already in: a copy next to the original.
    const size_t slash = dst->rfind('/');
    const std::string dir = dst->substr(0, slash), name = dst->substr(slash + 1);
    *dst = join(dir, copy_name(dir, name));
    return true;
  }
  if (it.type == 1 && S_ISDIR(ds.st_mode)) return true;  // folders merge
  ConflictInfo ci{it.src, *dst, it.size, static_cast<uint64_t>(ds.st_size), it.mtime, ds.st_mtim.tv_sec, S_ISDIR(ds.st_mode)};
  Conflict c = opt_.on_conflict ? opt_.on_conflict(ci) : Conflict::KeepBoth;
  switch (c) {
    case Conflict::Cancel: cancel_ = true; return false;
    case Conflict::Skip: *skip = true; return true;
    case Conflict::KeepBoth: {
      const size_t slash = dst->rfind('/');
      const std::string dir = dst->substr(0, slash), name = dst->substr(slash + 1);
      *dst = join(dir, unique_name(dir, name));
      return true;
    }
    case Conflict::Replace:
      if (S_ISDIR(ds.st_mode) && it.type != 1) {
        fail(*dst, "Cannot replace a folder with a file");
        *skip = true;
      }
      return true;
  }
  return true;
}

bool Transfer::verify_file(const std::string& path, uint64_t size, std::string* hex, char* buf) {
  int fd = -1;
  bool direct = false;
  if (opt_.direct_verify) {
    fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECT);
    direct = fd >= 0;
  }
  if (fd < 0) fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return false;
  if (!direct) ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
  if (!buf) {
    ::close(fd);
    return false;
  }
  Hasher h(opt_.algo);
  uint64_t got = 0;
  uint64_t unreported = 0;
  for (;;) {
    if (cancel_ || !wait_if_paused()) {
      ::close(fd);
      return false;
    }
    ssize_t n = ::read(fd, buf, opt_.block_bytes);
    if (n < 0 && errno == EINTR) continue;
    if (n < 0 && direct && errno == EINVAL) {  // the file system refused direct reads: use the page cache
      ::close(fd);
      fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
      if (fd < 0) return false;
      ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
      direct = false;
      h.reset();
      note(0, 0);
      got = 0;
      continue;
    }
    if (n < 0) {
      ::close(fd);
      return false;
    }
    if (n == 0) break;
    h.update(buf, static_cast<size_t>(n));
    got += static_cast<uint64_t>(n);
    unreported += static_cast<uint64_t>(n);
    if (unreported >= (4u << 20) || got == size) {  // progress is noted per few MiB, not per block: it takes a lock
      note(0, unreported);
      unreported = 0;
    }
  }
  if (unreported) note(0, unreported);
  ::close(fd);
  *hex = h.finish_hex();
  return got == size;
}

Transfer::Check Transfer::read_back(const Pending& p, std::string* got, char* buf) {
  if (!verify_file(p.tmp, p.size, got, buf)) return cancel_ ? Check::Cancelled : Check::Unreadable;
  return *got == p.want ? Check::Match : Check::Mismatch;
}

// Applies the outcome of read_back: rename into place, or discard and report. false when the transfer was cancelled.
bool Transfer::settle(const Pending& p, Check c) {
  switch (c) {
    case Check::Cancelled: ::unlink(p.tmp.c_str()); return false;
    case Check::Unreadable:
      ::unlink(p.tmp.c_str());
      fail(p.final_dst, "The copy could not be read back to check it");
      return true;
    case Check::Mismatch:
      ++result_.mismatches;
      result_.verified.push_back({p.src, p.final_dst, p.want, false});
      ::unlink(p.tmp.c_str());
      fail(p.final_dst, "The copy does not match the original (checksum differs); it was discarded");
      return true;
    case Check::Match: break;
  }
  result_.verified.push_back({p.src, p.final_dst, p.want, true});
  if (::rename(p.tmp.c_str(), p.final_dst.c_str()) != 0) {
    fail(p.final_dst, std::strerror(errno));
    ::unlink(p.tmp.c_str());
    return true;
  }
  ++result_.files_copied;
  result_.bytes_copied += p.size;
  if (opt_.move && ::unlink(p.src.c_str()) != 0) fail(p.src, std::string("Copied, but the original could not be removed: ") + std::strerror(errno));
  return true;
}

bool Transfer::finish_file(const Pending& p) {
  {
    std::lock_guard<std::mutex> l(mu_);
    prog_.phase = Phase::Verifying;
  }
  std::string got;
  const Check c = read_back(p, &got, static_cast<char*>(io_buf_));
  return settle(p, c);
}

bool Transfer::flush_batch() {
  if (pending_.empty()) return true;
  std::vector<Pending> batch;
  batch.swap(pending_);
  pending_bytes_ = 0;
  if (opt_.sync != SyncMode::Never) {
    {
      std::lock_guard<std::mutex> l(mu_);
      prog_.phase = Phase::Finishing;
    }
    // One flush for the whole batch: everything written so far on this file system reaches the device together.
    const size_t slash = batch[0].tmp.rfind('/');
    const int dfd = ::open(batch[0].tmp.substr(0, slash == std::string::npos ? 0 : slash + 1).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const bool ok = dfd >= 0 && ::syncfs(dfd) == 0;
    const int e = errno;
    if (dfd >= 0) ::close(dfd);
    if (!ok) {
      for (const Pending& p : batch) {
        ::unlink(p.tmp.c_str());
        fail(p.final_dst, std::string("Writing to the device failed: ") + std::strerror(e));
      }
      return true;
    }
  }
  {
    std::lock_guard<std::mutex> l(mu_);
    prog_.phase = Phase::Verifying;
  }
  // Reading a small file back from the device costs one round trip to it, whatever the file size. Several reads in flight at once
  // hide most of that latency (20,000 files: about 3x faster with four threads on this VM's virtual disk); the renames stay serial.
  std::vector<Check> checks(batch.size(), Check::Cancelled);
  std::vector<std::string> got(batch.size());
  const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
  const size_t workers = batch.size() < 8 ? 1 : std::min<size_t>(std::clamp(hw * 2, 2u, 8u), batch.size());
  if (workers <= 1) {
    for (size_t i = 0; i < batch.size() && !cancel_; ++i) checks[i] = read_back(batch[i], &got[i], static_cast<char*>(io_buf_));
  } else {
    std::atomic<size_t> next{0};
    auto work = [&] {
      void* mine = nullptr;  // every thread reads into a buffer of its own (the shared one belongs to the copy loop)
      if (posix_memalign(&mine, 4096, opt_.block_bytes) != 0) return;
      for (size_t i = next++; i < batch.size() && !cancel_; i = next++) checks[i] = read_back(batch[i], &got[i], static_cast<char*>(mine));
      std::free(mine);
    };
    std::vector<std::thread> pool;
    for (size_t k = 1; k < workers; ++k) pool.emplace_back(work);
    work();
    for (std::thread& t : pool) t.join();
  }
  for (size_t i = 0; i < batch.size(); ++i) {
    if (!settle(batch[i], checks[i])) {
      for (size_t k = i + 1; k < batch.size(); ++k) ::unlink(batch[k].tmp.c_str());
      return false;
    }
  }
  return true;
}

bool Transfer::copy_file(const Item& it, std::string* final_dst) {
  int in_fd = ::open(it.src.c_str(), O_RDONLY | O_CLOEXEC | O_NOATIME);
  if (in_fd < 0) in_fd = ::open(it.src.c_str(), O_RDONLY | O_CLOEXEC);
  if (in_fd < 0) {
    fail(it.src, std::strerror(errno));
    return true;
  }
  const size_t slash = final_dst->rfind('/');
  const std::string tmp = final_dst->substr(0, slash + 1) + "." + final_dst->substr(slash + 1) + ".fleetfm-part";
  const int out = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (out < 0) {
    fail(*final_dst, std::strerror(errno));
    ::close(in_fd);
    return true;
  }
  if (it.size >= (1u << 20)) {
    // Reserve the space first: a full disk fails now instead of at the end. Small files do not need the extra system call.
    ::posix_fadvise(in_fd, 0, 0, POSIX_FADV_SEQUENTIAL);
    const int fa = ::posix_fallocate(out, 0, static_cast<off_t>(it.size));
    if (fa == ENOSPC || fa == EFBIG) {
      fail(*final_dst, std::strerror(fa));
      ::close(in_fd);
      ::close(out);
      ::unlink(tmp.c_str());
      return true;
    }
  }
  auto abort_file = [&](const std::string& why) {
    ::close(in_fd);
    ::close(out);
    ::unlink(tmp.c_str());
    if (!why.empty()) fail(*final_dst, why);
  };

  Hasher src_hash(opt_.algo);
  bool ok = true;
  std::string why;
  uint64_t copied = 0, unreported = 0;
  auto note_chunk = [&](uint64_t n) {  // progress per few MiB, not per block: noting takes a lock
    unreported += n;
    if (unreported >= (4u << 20) || copied + n >= it.size) {
      note(unreported, 0);
      unreported = 0;
    }
  };
  if (!opt_.verify) {
    // Nothing to checksum: let the kernel move the data (no copy through user space, reflink where the file system has it).
    bool use_cfr = true;
    const uint64_t chunk = 8u << 20;
    while (copied < it.size) {
      if (cancel_ || !wait_if_paused()) {
        if (unreported) note(unreported, 0);
        abort_file("");
        return false;
      }
      if (use_cfr) {
        const ssize_t n = ::copy_file_range(in_fd, nullptr, out, nullptr, static_cast<size_t>(std::min<uint64_t>(chunk, it.size - copied)), 0);
        if (n > 0) {
          note_chunk(static_cast<uint64_t>(n));
          copied += static_cast<uint64_t>(n);
          continue;
        }
        if (n == 0) break;  // the source shrank
        if (errno == EINTR) continue;
        if (errno == EXDEV || errno == EINVAL || errno == ENOSYS || errno == EOPNOTSUPP || errno == EPERM) {
          use_cfr = false;
          continue;
        }
        ok = false;
        why = std::strerror(errno);
        break;
      }
      static thread_local std::vector<char> buf;
      buf.resize(opt_.block_bytes);
      const ssize_t n = ::read(in_fd, buf.data(), buf.size());
      if (n < 0 && errno == EINTR) continue;
      if (n < 0) {
        ok = false;
        why = std::strerror(errno);
        break;
      }
      if (n == 0) break;
      if (!write_all(out, buf.data(), static_cast<size_t>(n))) {
        ok = false;
        why = std::strerror(errno);
        break;
      }
      note_chunk(static_cast<uint64_t>(n));
      copied += static_cast<uint64_t>(n);
    }
  } else {
    char* buf = static_cast<char*>(io_buf_);
    uint64_t flushed_to = 0;
    constexpr uint64_t kStartWriteback = 16u << 20;
    const bool pipeline = it.size >= std::max<uint64_t>(opt_.big_file_bytes, 8u << 20);
    if (pipeline) {
      // Keep the source read/hash on this thread while a second thread drains a
      // two-buffer queue to the destination. This overlaps storage reads with
      // writes without sharing the output offset or the hashing state.
      struct Slot {
        std::vector<char> data;
        size_t size = 0;
        bool ready = false;
      } slots[2];
      for (Slot& slot : slots) slot.data.resize(opt_.block_bytes);
      std::mutex pipe_mu;
      std::condition_variable pipe_cv;
      bool producer_done = false, pipe_error = false;
      std::string pipe_why;
      std::thread writer([&] {
        size_t index = 0;
        for (;;) {
          std::unique_lock<std::mutex> lock(pipe_mu);
          pipe_cv.wait(lock, [&] { return slots[index].ready || producer_done; });
          if (!slots[index].ready) break;
          Slot& slot = slots[index];
          lock.unlock();
          if (!write_all(out, slot.data.data(), slot.size)) {
            lock.lock();
            pipe_error = true;
            pipe_why = std::strerror(errno);
            slot.ready = false;
            pipe_cv.notify_all();
            return;
          }
          lock.lock();
          slot.ready = false;
          lock.unlock();
          pipe_cv.notify_all();
          index ^= 1u;
        }
      });
      size_t index = 0;
      for (;;) {
        if (cancel_ || !wait_if_paused()) {
          ok = false;
          break;
        }
        Slot& slot = slots[index];
        {
          std::unique_lock<std::mutex> lock(pipe_mu);
          pipe_cv.wait(lock, [&] { return !slot.ready || pipe_error; });
          if (pipe_error) {
            ok = false;
            why = pipe_why;
            break;
          }
        }
        const ssize_t n = ::read(in_fd, slot.data.data(), slot.data.size());
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) {
          ok = false;
          why = std::strerror(errno);
          break;
        }
        if (n == 0) break;
        src_hash.update(slot.data.data(), static_cast<size_t>(n));
        {
          std::lock_guard<std::mutex> lock(pipe_mu);
          slot.size = static_cast<size_t>(n);
          slot.ready = true;
        }
        pipe_cv.notify_all();
        note_chunk(static_cast<uint64_t>(n));
        copied += static_cast<uint64_t>(n);
        index ^= 1u;
      }
      {
        std::lock_guard<std::mutex> lock(pipe_mu);
        producer_done = true;
      }
      pipe_cv.notify_all();
      writer.join();
      if (pipe_error && ok) {
        ok = false;
        why = pipe_why;
      }
    } else for (;;) {
      if (cancel_ || !wait_if_paused()) {
        if (unreported) note(unreported, 0);
        abort_file("");
        return false;
      }
      const ssize_t n = ::read(in_fd, buf, opt_.block_bytes);
      if (n < 0 && errno == EINTR) continue;
      if (n < 0) {
        ok = false;
        why = std::strerror(errno);
        break;
      }
      if (n == 0) break;
      src_hash.update(buf, static_cast<size_t>(n));
      if (!write_all(out, buf, static_cast<size_t>(n))) {
        ok = false;
        why = std::strerror(errno);
        break;
      }
      note_chunk(static_cast<uint64_t>(n));
      copied += static_cast<uint64_t>(n);
      // Big file: start the device writing while the rest is still being read and hashed, so the final fsync has little left to wait for.
      if (it.size >= opt_.big_file_bytes && opt_.sync != SyncMode::Never && copied - flushed_to >= kStartWriteback) {
        ::sync_file_range(out, static_cast<off64_t>(flushed_to), static_cast<off64_t>(copied - flushed_to), SYNC_FILE_RANGE_WRITE);
        flushed_to = copied;
      }
    }
  }
  if (unreported) note(unreported, 0);
  if (ok && copied != it.size && copied < it.size) {
    // The file changed while it was copied; the totals shown were a promise. Count what happened.
    std::lock_guard<std::mutex> l(mu_);
    prog_.bytes_total -= it.size - copied;
    if (opt_.verify) prog_.verify_total -= it.size - copied;
  }
  if (!ok) {
    abort_file(why);
    return true;
  }
  ::fchmod(out, it.mode);
  struct timespec ts[2] = {{0, UTIME_OMIT}, {static_cast<time_t>(it.mtime), 0}};
  ::futimens(out, ts);
  const bool immediate = !opt_.verify || it.size >= opt_.big_file_bytes;
  if (immediate && (opt_.sync == SyncMode::Always || (opt_.sync == SyncMode::Auto && opt_.verify))) {
    {
      std::lock_guard<std::mutex> l(mu_);
      prog_.phase = Phase::Finishing;
    }
    if (::fsync(out) != 0) {
      abort_file(std::string("Writing to the device failed: ") + std::strerror(errno));
      return true;
    }
  }
  ::close(in_fd);
  if (::close(out) != 0) {
    ::unlink(tmp.c_str());
    fail(*final_dst, std::string("Closing failed: ") + std::strerror(errno));
    return true;
  }

  if (opt_.verify) {
    Pending p{tmp, *final_dst, it.src, copied, src_hash.finish_hex()};
    if (immediate) return finish_file(p);
    // Small file: wait for the rest of the batch (see TransferOptions::big_file_bytes), so the flush and the read-back are shared.
    pending_bytes_ += copied;
    pending_.push_back(std::move(p));
    if (pending_bytes_ >= opt_.batch_bytes || pending_.size() >= opt_.batch_files) return flush_batch();
    return true;
  }
  if (::rename(tmp.c_str(), final_dst->c_str()) != 0) {
    fail(*final_dst, std::strerror(errno));
    ::unlink(tmp.c_str());
    return true;
  }
  ++result_.files_copied;
  result_.bytes_copied += copied;
  if (opt_.move) {
    if (::unlink(it.src.c_str()) != 0) fail(it.src, std::string("Copied, but the original could not be removed: ") + std::strerror(errno));
  }
  return true;
}

TransferResult Transfer::run() {
  t0_ = now_s();
  if (!discover()) {
    result_.cancelled = true;
    std::lock_guard<std::mutex> l(mu_);
    prog_.phase = Phase::Done;
    return result_;
  }
  // (old destination prefix -> new one) for folders that were kept side by side with an existing one.
  std::vector<std::pair<std::string, std::string>> remap;
  std::vector<std::string> skipped_dirs;  // destination prefixes of folders the user chose to skip
  std::vector<const Item*> made_dirs_for_move;
  for (const Item& it : plan_) {
    if (cancel_) break;
    std::string dst = it.dst;
    for (const auto& [from, to] : remap)
      if (dst == from || dst.compare(0, from.size() + 1, from + "/") == 0) {
        dst = to + dst.substr(from.size());
        break;
      }
    {
      std::lock_guard<std::mutex> l(mu_);
      prog_.current = it.src;
      prog_.phase = Phase::Copying;
    }
    bool skip = false;
    for (const std::string& sd : skipped_dirs)
      if (dst.compare(0, sd.size() + 1, sd + "/") == 0) skip = true;
    const std::string before = dst;
    if (!skip && !resolve_conflict(it, &dst, &skip)) break;
    if (dst != before) remap.emplace_back(before, dst);
    if (!skip && std::filesystem::path(it.dst).parent_path() == std::filesystem::path(dest_dir_)) result_.top_level.emplace_back(it.src, dst);
    if (skip) {
      if (it.type == 1) skipped_dirs.push_back(before);
      std::lock_guard<std::mutex> l(mu_);
      if (it.type != 1) {
        ++prog_.files_done;
        prog_.bytes_done += it.size;
        if (opt_.verify) prog_.verified += it.size;
      }
      continue;
    }
    if (it.type == 1) {
      if (::mkdir(dst.c_str(), 0700) != 0 && errno != EEXIST) {
        fail(dst, std::strerror(errno));
        continue;
      }
      made_dirs_for_move.push_back(&it);
      continue;
    }
    if (it.type == 2) {
      char target[4096];
      const ssize_t n = ::readlink(it.src.c_str(), target, sizeof target - 1);
      if (n < 0) {
        fail(it.src, std::strerror(errno));
        continue;
      }
      target[n] = 0;
      if (exists(dst)) ::unlink(dst.c_str());
      if (::symlink(target, dst.c_str()) != 0) {
        fail(dst, std::strerror(errno));
        continue;
      }
      if (opt_.move) ::unlink(it.src.c_str());
      std::lock_guard<std::mutex> l(mu_);
      ++prog_.files_done;
      continue;
    }
    if (!copy_file(it, &dst)) break;
    std::lock_guard<std::mutex> l(mu_);
    ++prog_.files_done;
  }
  // Files still waiting for their check (small files are checked in batches) are checked now; on cancel their temporary files go.
  if (cancel_) {
    for (const Pending& p : pending_) ::unlink(p.tmp.c_str());
    pending_.clear();
  } else if (!flush_batch()) {
    for (const Pending& p : pending_) ::unlink(p.tmp.c_str());
    pending_.clear();
  }
  // Folders: restore permissions and times once their contents are in, and drop the emptied sources of a move.
  for (auto i = made_dirs_for_move.rbegin(); i != made_dirs_for_move.rend(); ++i) {
    const Item& it = **i;
    std::string dst = it.dst;
    for (const auto& [from, to] : remap)
      if (dst == from || dst.compare(0, from.size() + 1, from + "/") == 0) {
        dst = to + dst.substr(from.size());
        break;
      }
    ::chmod(dst.c_str(), it.mode);
    struct timespec ts[2] = {{0, UTIME_OMIT}, {static_cast<time_t>(it.mtime), 0}};
    ::utimensat(AT_FDCWD, dst.c_str(), ts, 0);
    if (opt_.move && result_.errors.empty() && !cancel_) ::rmdir(it.src.c_str());
  }
  result_.cancelled = cancel_;
  std::lock_guard<std::mutex> l(mu_);
  prog_.phase = Phase::Done;
  if (!result_.cancelled && prog_.bytes_total > 0) {
    prog_.bytes_done = prog_.bytes_total;
    prog_.verified = prog_.verify_total;
  }
  return result_;
}

}  // namespace fleetwm::fm
