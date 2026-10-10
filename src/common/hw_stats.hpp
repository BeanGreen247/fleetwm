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
};
bool parse_meminfo(const std::string& text, MemDetail* out);

struct DiskCounters {
  unsigned long long read_bytes = 0, write_bytes = 0;
};
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
  long long vram_used = -1, vram_total = -1;  // bytes
};
// AMD: gpu_busy_percent, hwmon power1_average/input, freq1_input (sclk), freq2_input (mclk), mem_info_vram_*.
// `device_dir` is /sys/class/drm/cardN/device.
GpuDetail read_amd_gpu_detail(const std::string& device_dir);
// One line per card of `nvidia-smi --query-gpu=utilization.gpu,power.draw,clocks.gr,clocks.mem,memory.used,memory.total
// --format=csv,noheader,nounits` ("[N/A]" fields stay -1). Memory is MiB in the output, bytes in the result.
std::vector<GpuDetail> parse_nvidia_smi_csv(const std::string& text);

}  // namespace fleetwm
