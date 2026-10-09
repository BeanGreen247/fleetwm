#pragma once

// Process and machine counters for the Task Manager: pure parsers (tested on fixture text), a process table that turns two samples into
// CPU and disk rates, a short history for graphs, and size formatting. Reading /proc keeps one file open per process and uses pread, so a
// refresh is a directory scan plus two preads per process and no allocation per process once the table is warm.

#include <string>
#include <unordered_map>
#include <vector>

#include "hw_stats.hpp"

namespace fleetwm {

struct ProcInfo {
  int pid = 0;
  std::string name;  // comm, at most 15 characters
  unsigned uid = 0;
  char state = '?';
  int threads = 1;
  unsigned long long cpu_ticks = 0;  // utime + stime in clock ticks
  long long rss_kb = 0;
  unsigned long long read_bytes = 0, write_bytes = 0;  // from /proc/PID/io; 0 when it cannot be read (another user's process)
};

// One /proc/PID/stat line. The name sits in parentheses and may itself contain spaces and parentheses, so it ends at the LAST ')'.
bool parse_pid_stat(const std::string& text, long page_size_kb, ProcInfo* out);
// /proc/PID/io: read_bytes and write_bytes (what really touched the disk).
bool parse_pid_io(const std::string& text, unsigned long long* read_bytes, unsigned long long* write_bytes);
// The aggregate "cpu" line of /proc/stat.
bool parse_proc_stat_total(const std::string& text, CpuTimes* out);

struct NetCounters {
  unsigned long long rx_bytes = 0, tx_bytes = 0;
};
// Sum over every interface of /proc/net/dev except loopback.
NetCounters parse_net_dev(const std::string& text);

// "1.4 GB", "312 MB", "56 KB" (binary units, one decimal from GB up) from KiB.
std::string format_kib(long long kib);

struct ProcRow {
  ProcInfo info;
  double cpu_percent = 0;  // share of the whole machine (all cores = 100)
  double disk_bps = 0;     // read + write bytes per second
  bool fresh = true;       // appeared since the last refresh: no rate yet
};

enum class ProcColumn { Name, Pid, Cpu, Memory, Disk, User };
// Sorts in place; ties break on name, then pid, so the list does not jump around between refreshes.
void sort_rows(std::vector<ProcRow>* rows, ProcColumn column, bool descending, const std::unordered_map<unsigned, std::string>& user_names);

class ProcessTable {
 public:
  // Takes a new sample (already read) and computes rates against the previous one. `dt` seconds apart, `hz` clock ticks/s, `cpus` cores.
  void apply(const std::vector<ProcInfo>& now, double dt, long hz, int cpus);
  // Reads /proc and applies. Returns false when /proc could not be read.
  bool refresh(double dt, long hz, int cpus);
  const std::vector<ProcRow>& rows() const { return rows_; }
  ~ProcessTable();

 private:
  struct Files {
    int stat_fd = -1, io_fd = -1;
    bool seen = false;
  };
  std::vector<ProcRow> rows_;
  std::unordered_map<int, ProcInfo> previous_;
  std::unordered_map<int, Files> files_;
  void* dir_ = nullptr;  // DIR*, kept open
  long page_kb_ = 4;
};

// The last N samples of one number, oldest first, for a graph.
class History {
 public:
  explicit History(size_t capacity = 60) : cap_(capacity) {}
  void push(double v) {
    if (values_.size() == cap_) values_.erase(values_.begin());
    values_.push_back(v);
  }
  const std::vector<double>& values() const { return values_; }
  double max() const {
    double m = 0;
    for (double v : values_) m = v > m ? v : m;
    return m;
  }
  size_t capacity() const { return cap_; }

 private:
  size_t cap_;
  std::vector<double> values_;
};

// The "audio latency" the Performance tab shows: quantum frames at a sample rate, from `pw-metadata -n settings` text (clock.quantum and
// clock.rate lines); -1 when either is missing.
double parse_audio_latency_ms(const std::string& pw_metadata_output);

}  // namespace fleetwm
