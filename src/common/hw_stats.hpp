#pragma once

// Hardware readings behind the bar's metric tooltips: per-core CPU load, memory breakdown, disk throughput,
// power draw and GPU details. Pure parsers plus small sysfs readers; directory arguments exist so tests can
// point them at fake trees.

#include <string>
#include <vector>

namespace fleetwm {

struct CpuTimes {
  unsigned long long busy = 0, total = 0;
};
struct CpuInfo {
  std::string model;
  int logical_threads = 0;
  int physical_cores = 0;
  int mhz = 0;
};
CpuInfo parse_cpuinfo(const std::string& text);

struct CpuCacheInfo {
  int level = 0;
  std::string type;
  std::string size;
  int line_bytes = 0;
  int ways = 0;
  std::string shared_cpus;
};
std::vector<CpuCacheInfo> parse_cpu_cache_info(const std::string& text);
std::string format_bandwidth_gbs(double bytes_per_second);

struct MemoryModule {
  std::string locator, manufacturer, part_number, type;
  long long size_mb = 0;
  int speed_mhz = 0;
};
std::vector<MemoryModule> parse_memory_devices(const std::string& text);

struct DriveInfo {
  std::string name, model, vendor, serial, transport;
  long long size_bytes = 0;
};
std::vector<DriveInfo> parse_drive_inventory(const std::string& text);
// The "cpuN" lines of /proc/stat in order (the aggregate "cpu" line is skipped).
std::vector<CpuTimes> parse_proc_stat_cores(const std::string& text);
// Load of each core between two samples, 0..100; -1 where there was no time between them.
std::vector<int> core_percents(const std::vector<CpuTimes>& prev, const std::vector<CpuTimes>& cur);

struct MemDetail {
  unsigned long long total = 0, free = 0, available = 0, cached = 0, buffers = 0, swap_total = 0, swap_free = 0;  // KiB
  unsigned long long shmem = 0, sreclaimable = 0, sunreclaim = 0, anon = 0, mapped = 0, dirty = 0, writeback = 0, committed = 0, commit_limit = 0;
};
// The columns of `free`, from the same /proc/meminfo fields procps uses:
//   used        total - available (programs, kernel, and cache that cannot be given back)
//   free        MemFree
//   shared      Shmem (tmpfs, shared memory; it is also counted inside buff_cache, as `free` does)
//   buff_cache  Buffers + Cached + SReclaimable
//   available   what a new program can get without swapping
// `reclaimable` = available - free is the part of the cache that can be dropped; used + reclaimable + free = total, for the usage bar.
struct MemComposition {
  unsigned long long used = 0, free = 0, shared = 0, buff_cache = 0, available = 0, reclaimable = 0, total = 0;
};
MemComposition mem_composition(const MemDetail& m);
bool parse_meminfo(const std::string& text, MemDetail* out);

// Traffic between the CPU/GPU and RAM, from the memory controller's own counters (the uncore_imc perf events data_reads and data_writes,
// which count 64-byte lines and carry a scale to MiB). The kernel hands them to root or to a user when kernel.perf_event_paranoid is
// at most 0 (or CAP_PERFMON); without that, status() says why and the rates stay -1. Call sample() about once a second.
// parse_pmu_scale reads the text of an `events/*.scale` file ("6.103515625e-5").
bool parse_pmu_scale(const std::string& text, double* scale);
class ImcBandwidth {
 public:
  ImcBandwidth() = default;
  ~ImcBandwidth();
  ImcBandwidth(const ImcBandwidth&) = delete;
  ImcBandwidth& operator=(const ImcBandwidth&) = delete;
  // `root` is /sys/bus/event_source/devices/uncore_imc, a fake tree in tests.
  void open(const std::string& root = "/sys/bus/event_source/devices/uncore_imc");
  void sample();
  bool ok() const { return read_fd_ >= 0 && write_fd_ >= 0; }
  double read_mib_s() const { return read_rate_; }    // -1 until two samples exist
  double write_mib_s() const { return write_rate_; }
  const std::string& status() const { return status_; }

 private:
  int read_fd_ = -1, write_fd_ = -1;
  double read_scale_ = 0, write_scale_ = 0, read_rate_ = -1, write_rate_ = -1;
  unsigned long long prev_read_ = 0, prev_write_ = 0;
  double prev_time_ = -1;
  std::string status_ = "not opened";
};

struct DiskCounters {
  unsigned long long read_bytes = 0, write_bytes = 0;
};
// The request counts and times of the same whole disks, for latency and busy time.
struct DiskActivity {
  unsigned long long reads = 0, writes = 0, read_ms = 0, write_ms = 0, io_ms = 0, in_flight = 0;  // ms: time requests spent being served, and time the disk had any
};
DiskActivity parse_disk_activity(const std::string& text);
// Average time one request took between two samples (waiting plus service, like iostat's await); -1 when nothing completed.
double disk_await_ms(const DiskActivity& prev, const DiskActivity& cur);
// Share of the interval the disk had work queued (iostat's %util), 0..100; `dt` in seconds.
double disk_busy_percent(const DiskActivity& prev, const DiskActivity& cur, double dt);

// The "cpu" line and the counters under it from /proc/stat.
struct CpuBreakdown {
  unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
  unsigned long long ctxt = 0, intr = 0, forks = 0;
  int running = 0, blocked = 0;  // right now
  unsigned long long total() const { return user + nice + system + idle + iowait + irq + softirq + steal; }
};
bool parse_proc_stat_breakdown(const std::string& text, CpuBreakdown* out);
struct CpuRates {
  double user = 0, system = 0, iowait = 0, irq = 0, steal = 0;  // share of the interval, percent
  double ctxt_per_s = 0, intr_per_s = 0, forks_per_s = 0;
  bool valid = false;
};
CpuRates cpu_rates(const CpuBreakdown& prev, const CpuBreakdown& cur, double dt);

// /proc/pressure/{cpu,io,memory}: the share of time some (or all) tasks were stalled waiting for that resource. -1 where the file has no such line.
struct Pressure {
  double some10 = -1, some60 = -1, full10 = -1, full60 = -1;
};
bool parse_pressure(const std::string& text, Pressure* out);

// "0: 300Mhz\n1: 700Mhz *\n2: 1200Mhz" (AMD pp_dpm_sclk / pp_dpm_mclk): the active level and the highest one, in MHz; false when empty.
bool parse_pp_dpm(const std::string& text, int* current_mhz, int* max_mhz);
// Sum over whole disks only (sda, nvme0n1, vda, mmcblk0; not their partitions, loop, ram or dm devices).
DiskCounters parse_diskstats(const std::string& text);
bool is_whole_disk(const std::string& name);

// "1.4 MB/s", "312 KB/s", "0 B/s"; decimal units, like disk vendors and `iostat`.
std::string format_rate(double bytes_per_s);

// Watts drawn by the whole machine on battery (BAT*/power_now, or current_now * voltage_now); -1 if unknown.
double battery_power_watts(const std::string& supply_dir);
// CPU package energy counter in microjoules (Intel/AMD RAPL); -1 if absent or unreadable (needs root on newer kernels).
long long rapl_energy_uj(const std::string& powercap_dir);

// How late a 200 us sleep wakes up, best of `tries`, in microseconds.
double timer_latency_us(int tries = 5);

struct GpuDetail {
  int percent = -1;
  double power_w = -1;
  int core_mhz = -1, mem_mhz = -1;
  int core_max_mhz = -1, mem_max_mhz = -1;
  int mem_busy_percent = -1;  // the memory controller's share of time busy (AMD mem_busy_percent, NVIDIA utilization.memory): the nearest thing to bandwidth use these drivers report
  long long vram_used = -1, vram_total = -1;  // bytes
  bool asleep = false;  // the card is in runtime suspend and was left alone: nothing above is a reading
};
// AMD: gpu_busy_percent, hwmon power1_average/input, freq1_input (sclk), freq2_input (mclk), mem_info_vram_*.
// `device_dir` is /sys/class/drm/cardN/device.
GpuDetail read_amd_gpu_detail(const std::string& device_dir);
// One line per card of `nvidia-smi --query-gpu=utilization.gpu,power.draw,clocks.gr,clocks.mem,memory.used,memory.total
// --format=csv,noheader,nounits` ("[N/A]" fields stay -1). Memory is MiB in the output, bytes in the result.
std::vector<GpuDetail> parse_nvidia_smi_csv(const std::string& text);

}  // namespace fleetwm
