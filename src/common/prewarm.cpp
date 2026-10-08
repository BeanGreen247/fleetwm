#include "prewarm.hpp"

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

namespace fleetwm::prewarm {

namespace {

constexpr uint64_t kPage = 4096;
constexpr uint64_t kMaxGap = 64 * 1024;           // reading 16 pages we do not need beats a second request
constexpr uint64_t kMaxTotal = 48ull * 1024 * 1024;  // a manifest never asks for more than this in all
constexpr int kRecordDelaySeconds = 4;            // start-up is over by then; anything touched later is not start-up

bool plain_path(const std::string& p) { return !p.empty() && p[0] == '/' && p.find_first_of("\t\n\r") == std::string::npos; }

}  // namespace

std::string format_manifest(const std::vector<Range>& ranges) {
  std::string out;
  for (const Range& r : ranges) {
    if (!plain_path(r.path) || r.length == 0) continue;
    out += r.path + '\t' + std::to_string(r.offset) + '\t' + std::to_string(r.length) + '\n';
  }
  return out;
}

std::vector<Range> parse_manifest(const std::string& text) {
  std::vector<Range> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    const size_t a = line.find('\t');
    const size_t b = a == std::string::npos ? a : line.find('\t', a + 1);
    if (b == std::string::npos) continue;
    Range r;
    r.path = line.substr(0, a);
    char* end = nullptr;
    const std::string off = line.substr(a + 1, b - a - 1), len = line.substr(b + 1);
    if (off.empty() || len.empty() || off.find_first_not_of("0123456789") != std::string::npos ||
        len.find_first_not_of("0123456789") != std::string::npos)
      continue;
    r.offset = std::strtoull(off.c_str(), &end, 10);
    r.length = std::strtoull(len.c_str(), &end, 10);
    if (!plain_path(r.path) || r.length == 0) continue;
    out.push_back(std::move(r));
  }
  return out;
}

std::vector<Range> coalesce(std::vector<Range> ranges, uint64_t max_gap, uint64_t max_total) {
  std::sort(ranges.begin(), ranges.end(), [](const Range& a, const Range& b) {
    return a.path != b.path ? a.path < b.path : a.offset < b.offset;
  });
  std::vector<Range> out;
  for (const Range& r : ranges) {
    if (r.length == 0) continue;
    if (!out.empty() && out.back().path == r.path && r.offset <= out.back().offset + out.back().length + max_gap) {
      const uint64_t end = std::max(out.back().offset + out.back().length, r.offset + r.length);
      out.back().length = end - out.back().offset;
    } else {
      out.push_back(r);
    }
  }
  uint64_t total = 0;
  std::vector<Range> kept;
  for (const Range& r : out) {
    if (total >= max_total) break;
    Range k = r;
    k.length = std::min(k.length, max_total - total);
    total += k.length;
    kept.push_back(std::move(k));
  }
  return kept;
}

std::vector<Range> resident_file_ranges(const std::string& maps_path, const std::string& pagemap_path) {
  std::vector<Range> out;
  std::ifstream maps(maps_path);
  const int pm = open(pagemap_path.c_str(), O_RDONLY | O_CLOEXEC);
  if (!maps || pm < 0) {
    if (pm >= 0) close(pm);
    return out;
  }
  std::string line;
  while (std::getline(maps, line)) {
    // 7f1c2a000000-7f1c2a021000 r-xp 00001000 fd:01 1234  /usr/lib/x86_64-linux-gnu/libfoo.so
    unsigned long long lo = 0, hi = 0, file_off = 0, inode = 0;
    char perms[8] = {};
    char dev[16] = {};
    int consumed = 0;
    if (std::sscanf(line.c_str(), "%llx-%llx %7s %llx %15s %llu %n", &lo, &hi, perms, &file_off, dev, &inode, &consumed) < 6) continue;
    if (inode == 0 || consumed <= 0) continue;
    std::string path = line.substr(static_cast<size_t>(consumed));
    static const std::string kDeleted = " (deleted)";
    const bool deleted = path.size() > kDeleted.size() && path.compare(path.size() - kDeleted.size(), kDeleted.size(), kDeleted) == 0;
    if (!plain_path(path) || deleted) continue;
    if (perms[0] != 'r') continue;
    const uint64_t pages = (hi - lo) / kPage;
    std::vector<uint64_t> entries(pages);
    const ssize_t want = static_cast<ssize_t>(pages * sizeof(uint64_t));
    if (pread(pm, entries.data(), static_cast<size_t>(want), static_cast<off_t>(lo / kPage * sizeof(uint64_t))) != want) continue;
    for (uint64_t i = 0; i < pages; ++i) {
      if (!(entries[i] >> 63)) continue;  // bit 63: present in this process's page tables
      // bit 61: file-page or shared-anon; a private copy-on-write page (written data, relocations) is anonymous now and has no file range
      if (!((entries[i] >> 61) & 1)) continue;
      Range r;
      r.path = path;
      r.offset = file_off + i * kPage;
      r.length = kPage;
      out.push_back(std::move(r));
    }
  }
  close(pm);
  return coalesce(std::move(out), kMaxGap, kMaxTotal);
}

size_t replay(const std::vector<Range>& ranges) {
  size_t requests = 0;
  std::string open_path;
  int fd = -1;
  for (const Range& r : ranges) {
    if (r.path != open_path) {
      if (fd >= 0) close(fd);
      fd = open(r.path.c_str(), O_RDONLY | O_CLOEXEC | O_NOATIME);
      if (fd < 0) fd = open(r.path.c_str(), O_RDONLY | O_CLOEXEC);  // O_NOATIME needs file ownership
      open_path = r.path;
    }
    if (fd < 0) continue;
    posix_fadvise(fd, static_cast<off_t>(r.offset), static_cast<off_t>(r.length), POSIX_FADV_WILLNEED);
    ++requests;
  }
  if (fd >= 0) close(fd);
  return requests;
}

std::vector<Range> file_heads(const std::vector<Range>& ranges, uint64_t bytes) {
  std::vector<Range> out;
  std::string last;
  for (const Range& r : ranges) {
    if (r.path == last) continue;
    last = r.path;
    struct stat st {};
    if (stat(r.path.c_str(), &st) != 0 || st.st_size <= 0) continue;
    const uint64_t size = static_cast<uint64_t>(st.st_size);
    Range h;
    h.path = r.path;
    h.length = (std::min(bytes, size) + kPage - 1) / kPage * kPage;
    out.push_back(std::move(h));
  }
  return out;
}

std::string manifest_path_for(const std::string& exe_path, const std::string& program) {
  std::string dir;
  if (const char* x = std::getenv("XDG_CACHE_HOME"); x && *x == '/') dir = x;
  else if (const char* h = std::getenv("HOME"); h && *h == '/') dir = std::string(h) + "/.cache";
  else return "";
  struct stat st {};
  if (stat(exe_path.c_str(), &st) != 0) return "";
  return dir + "/fleetwm/prewarm/" + program + "-" + std::to_string(static_cast<unsigned long long>(st.st_size)) + "-" +
         std::to_string(static_cast<long long>(st.st_mtime)) + ".manifest";
}

std::string manifest_path(const std::string& program) { return manifest_path_for("/proc/self/exe", program); }

size_t prewarm_program(const std::string& exe_path, const std::string& program) {
  const char* mode = std::getenv("FLEETWM_PREWARM");
  if (mode && std::strcmp(mode, "off") == 0) return 0;
  const std::string path = manifest_path_for(exe_path, program);
  if (path.empty()) return 0;
  std::ifstream in(path);
  if (!in) return 0;
  std::stringstream ss;
  ss << in.rdbuf();
  return replay(parse_manifest(ss.str()));
}

void prewarm_programs_async(std::vector<std::pair<std::string, std::string>> programs) {
  const char* mode = std::getenv("FLEETWM_PREWARM");
  if (mode && std::strcmp(mode, "off") == 0) return;
  std::thread([programs = std::move(programs)] {
    sigset_t all;
    sigfillset(&all);
    pthread_sigmask(SIG_BLOCK, &all, nullptr);
    // ioprio_set(IOPRIO_WHO_PROCESS, this thread, class IDLE): the main thread's own disk reads go first.
    syscall(SYS_ioprio_set, 1, static_cast<int>(syscall(SYS_gettid)), 3 << 13);
    for (const auto& [exe, name] : programs) prewarm_program(exe, name);
  }).detach();
}

namespace {

void record_to(const std::string& path) {
  std::vector<Range> ranges = resident_file_ranges();
  if (ranges.empty()) return;
  const std::vector<Range> heads = file_heads(ranges, 256 * 1024);
  ranges.insert(ranges.end(), heads.begin(), heads.end());
  ranges = coalesce(std::move(ranges), kMaxGap, kMaxTotal);
  const size_t slash = path.rfind('/');
  // mkdir -p of the three levels under the cache directory
  std::string dir = path.substr(0, slash);
  for (size_t i = 1; i <= dir.size(); ++i)
    if (i == dir.size() || dir[i] == '/') mkdir(dir.substr(0, i).c_str(), 0700);
  const std::string tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) return;
    out << format_manifest(ranges);
    if (!out) return;
  }
  rename(tmp.c_str(), path.c_str());  // atomic: a program starting now sees the old manifest or the new one, never half
}

}  // namespace

void start(const char* program) {
  const char* mode = std::getenv("FLEETWM_PREWARM");
  if (mode && std::strcmp(mode, "off") == 0) return;
  const std::string path = manifest_path(program ? program : "");
  if (path.empty()) return;
  const bool force_record = mode && std::strcmp(mode, "record") == 0;
  if (!force_record && access(path.c_str(), R_OK) == 0) return;  // already learned for this build
  // No manifest yet for this build (or a re-record was asked for): learn at +4 s, once.
  std::thread([path] {
    sigset_t all;  // a signal sent to the program must reach its own handling (signalfd, sigwait), never this helper
    sigfillset(&all);
    pthread_sigmask(SIG_BLOCK, &all, nullptr);
    std::this_thread::sleep_for(std::chrono::seconds(kRecordDelaySeconds));
    record_to(path);
  }).detach();
}

}  // namespace fleetwm::prewarm
