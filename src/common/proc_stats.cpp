#include "proc_stats.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace fleetwm {

bool parse_pid_stat(const std::string& text, long page_size_kb, ProcInfo* out) {
  const size_t open = text.find('(');
  const size_t close = text.rfind(')');
  if (open == std::string::npos || close == std::string::npos || close < open) return false;
  ProcInfo p;
  p.pid = std::atoi(text.c_str());
  if (p.pid <= 0) return false;
  p.name = text.substr(open + 1, close - open - 1);
  // After ") ": state ppid pgrp session tty tpgid flags minflt cminflt majflt cmajflt utime stime cutime cstime priority nice threads itrealvalue
  // starttime vsize rss
  std::istringstream in(text.substr(close + 1));
  std::string f;
  std::vector<std::string> fields;
  while (in >> f) fields.push_back(f);
  if (fields.size() < 22) return false;
  p.state = fields[0][0];
  p.cpu_ticks = std::strtoull(fields[11].c_str(), nullptr, 10) + std::strtoull(fields[12].c_str(), nullptr, 10);
  p.threads = std::atoi(fields[17].c_str());
  p.rss_kb = std::strtoll(fields[21].c_str(), nullptr, 10) * page_size_kb;
  *out = std::move(p);
  return true;
}

bool parse_pid_io(const std::string& text, unsigned long long* read_bytes, unsigned long long* write_bytes) {
  bool r = false, w = false;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    unsigned long long v = 0;
    if (std::sscanf(line.c_str(), "read_bytes: %llu", &v) == 1) {
      *read_bytes = v;
      r = true;
    } else if (std::sscanf(line.c_str(), "write_bytes: %llu", &v) == 1) {
      *write_bytes = v;
      w = true;
    }
  }
  return r && w;
}

bool parse_proc_stat_total(const std::string& text, CpuTimes* out) {
  unsigned long long user = 0, nice = 0, sys = 0, idle = 0, iow = 0, irq = 0, sirq = 0, steal = 0;
  if (std::sscanf(text.c_str(), "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &user, &nice, &sys, &idle, &iow, &irq, &sirq, &steal) < 4) return false;
  out->total = user + nice + sys + idle + iow + irq + sirq + steal;
  out->busy = out->total - idle - iow;
  return true;
}

NetCounters parse_net_dev(const std::string& text) {
  NetCounters sum;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string name = line.substr(0, colon);
    name.erase(0, name.find_first_not_of(' '));
    if (name == "lo") continue;
    unsigned long long rx = 0, a, b, c, d, e, f, g, tx = 0;
    if (std::sscanf(line.c_str() + colon + 1, "%llu %llu %llu %llu %llu %llu %llu %llu %llu", &rx, &a, &b, &c, &d, &e, &f, &g, &tx) < 9) continue;
    sum.rx_bytes += rx;
    sum.tx_bytes += tx;
  }
  return sum;
}

std::string format_kib(long long kib) {
  char b[32];
  if (kib >= 1024LL * 1024) std::snprintf(b, sizeof b, "%.1f GB", static_cast<double>(kib) / 1048576.0);
  else if (kib >= 1024) std::snprintf(b, sizeof b, "%lld MB", kib / 1024);
  else std::snprintf(b, sizeof b, "%lld KB", kib < 0 ? 0 : kib);
  return b;
}

void sort_rows(std::vector<ProcRow>* rows, ProcColumn column, bool descending, const std::unordered_map<unsigned, std::string>& user_names) {
  auto user_of = [&](const ProcRow& r) {
    const auto it = user_names.find(r.info.uid);
    return it == user_names.end() ? std::to_string(r.info.uid) : it->second;
  };
  std::stable_sort(rows->begin(), rows->end(), [&](const ProcRow& a, const ProcRow& b) {
    auto cmp = [&]() -> int {  // negative: a first when ascending
      switch (column) {
        case ProcColumn::Name: return a.info.name.compare(b.info.name);
        case ProcColumn::Pid: return a.info.pid < b.info.pid ? -1 : a.info.pid > b.info.pid;
        case ProcColumn::Cpu: return a.cpu_percent < b.cpu_percent ? -1 : a.cpu_percent > b.cpu_percent;
        case ProcColumn::Memory: return a.info.rss_kb < b.info.rss_kb ? -1 : a.info.rss_kb > b.info.rss_kb;
        case ProcColumn::Disk: return a.disk_bps < b.disk_bps ? -1 : a.disk_bps > b.disk_bps;
        case ProcColumn::User: return user_of(a).compare(user_of(b));
      }
      return 0;
    }();
    if (cmp != 0) return descending ? cmp > 0 : cmp < 0;
    if (a.info.name != b.info.name) return a.info.name < b.info.name;
    return a.info.pid < b.info.pid;
  });
}

void ProcessTable::apply(const std::vector<ProcInfo>& now, double dt, long hz, int cpus) {
  rows_.clear();
  rows_.reserve(now.size());
  std::unordered_map<int, ProcInfo> next;
  next.reserve(now.size());
  for (const ProcInfo& p : now) {
    ProcRow r;
    r.info = p;
    const auto prev = previous_.find(p.pid);
    if (prev != previous_.end() && dt > 0 && prev->second.cpu_ticks <= p.cpu_ticks) {
      r.fresh = false;
      r.cpu_percent = 100.0 * static_cast<double>(p.cpu_ticks - prev->second.cpu_ticks) / (static_cast<double>(hz) * dt * std::max(1, cpus));
      const unsigned long long dr = p.read_bytes >= prev->second.read_bytes ? p.read_bytes - prev->second.read_bytes : 0;
      const unsigned long long dw = p.write_bytes >= prev->second.write_bytes ? p.write_bytes - prev->second.write_bytes : 0;
      r.disk_bps = static_cast<double>(dr + dw) / dt;
    }
    rows_.push_back(std::move(r));
    next.emplace(p.pid, p);
  }
  previous_ = std::move(next);
}

ProcessTable::~ProcessTable() {
  for (auto& [pid, f] : files_) {
    if (f.stat_fd >= 0) close(f.stat_fd);
    if (f.io_fd >= 0) close(f.io_fd);
  }
  if (dir_) closedir(static_cast<DIR*>(dir_));
}

bool ProcessTable::refresh(double dt, long hz, int cpus) {
  if (!dir_) {
    dir_ = opendir("/proc");
    page_kb_ = sysconf(_SC_PAGESIZE) / 1024;
  }
  if (!dir_) return false;
  DIR* d = static_cast<DIR*>(dir_);
  rewinddir(d);
  for (auto& [pid, f] : files_) f.seen = false;
  std::vector<ProcInfo> now;
  now.reserve(previous_.size() + 16);
  char buf[2048];
  while (dirent* e = readdir(d)) {
    if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
    const int pid = std::atoi(e->d_name);
    Files& f = files_[pid];
    f.seen = true;
    if (f.stat_fd < 0) {
      char path[64];
      std::snprintf(path, sizeof path, "/proc/%d/stat", pid);
      f.stat_fd = open(path, O_RDONLY | O_CLOEXEC);
      std::snprintf(path, sizeof path, "/proc/%d/io", pid);
      f.io_fd = open(path, O_RDONLY | O_CLOEXEC);  // -1 for other users' processes: shown as 0
    }
    if (f.stat_fd < 0) continue;
    const ssize_t n = pread(f.stat_fd, buf, sizeof buf - 1, 0);
    if (n <= 0) continue;
    buf[n] = 0;
    ProcInfo p;
    if (!parse_pid_stat(buf, page_kb_, &p)) continue;
    struct stat st {};
    if (fstat(f.stat_fd, &st) == 0) p.uid = st.st_uid;
    if (f.io_fd >= 0) {
      const ssize_t m = pread(f.io_fd, buf, sizeof buf - 1, 0);
      if (m > 0) {
        buf[m] = 0;
        parse_pid_io(buf, &p.read_bytes, &p.write_bytes);
      }
    }
    now.push_back(std::move(p));
  }
  for (auto it = files_.begin(); it != files_.end();) {  // processes that are gone: close their files
    if (it->second.seen) {
      ++it;
      continue;
    }
    if (it->second.stat_fd >= 0) close(it->second.stat_fd);
    if (it->second.io_fd >= 0) close(it->second.io_fd);
    it = files_.erase(it);
  }
  apply(now, dt, hz, cpus);
  return true;
}

double parse_audio_latency_ms(const std::string& text) {
  double quantum = -1, rate = -1;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    const size_t k = line.find("key:'");
    const size_t v = line.find("value:'");
    if (k == std::string::npos || v == std::string::npos) continue;
    const std::string key = line.substr(k + 5, line.find('\'', k + 5) - k - 5);
    const double val = std::atof(line.c_str() + v + 7);
    if (key == "clock.quantum") quantum = val;
    else if (key == "clock.rate") rate = val;
  }
  if (quantum <= 0 || rate <= 0) return -1;
  return 1000.0 * quantum / rate;
}

}  // namespace fleetwm
