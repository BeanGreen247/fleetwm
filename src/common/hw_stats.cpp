#include "hw_stats.hpp"

#include <dirent.h>
#include <time.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <tuple>

namespace fleetwm {

namespace {

void hw_trim(std::string* s) {
  const size_t first = s->find_first_not_of(" \t");
  const size_t last = s->find_last_not_of(" \t");
  if (first == std::string::npos) {
    s->clear();
  } else {
    *s = s->substr(first, last - first + 1);
  }
}

long long read_ll(const std::string& path) {
  long long v = -1;
  std::ifstream f(path);
  if (!(f >> v)) return -1;
  return v;
}

std::string first_hwmon(const std::string& device_dir) {
  const std::string base = device_dir + "/hwmon";
  std::string found;
  if (DIR* d = opendir(base.c_str())) {
    while (const dirent* e = readdir(d))
      if (std::strncmp(e->d_name, "hwmon", 5) == 0 && (found.empty() || base + "/" + e->d_name < found))
        found = base + "/" + e->d_name;
    closedir(d);
  }
  return found;
}

}  // namespace

CpuInfo parse_cpuinfo(const std::string& text) {
  CpuInfo out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string key = line.substr(0, colon), value = line.substr(colon + 1);
    hw_trim(&key);
    hw_trim(&value);
    if (key == "model name" && out.model.empty()) out.model = value;
    else if (key == "processor") ++out.logical_threads;
    else if (key == "cpu MHz" && out.mhz == 0) out.mhz = static_cast<int>(std::strtod(value.c_str(), nullptr) + 0.5);
    else if (key == "cpu cores" && out.physical_cores == 0) out.physical_cores = std::atoi(value.c_str());
  }
  return out;
}

std::vector<CpuCacheInfo> parse_cpu_cache_info(const std::string& text) {
  std::vector<CpuCacheInfo> out;
  CpuCacheInfo current;
  bool active = false;
  auto finish = [&] {
    if (active && current.level > 0 && !current.size.empty()) out.push_back(current);
    current = {};
    active = false;
  };
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    hw_trim(&line);
    if (line.empty()) {
      finish();
      continue;
    }
    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string key = line.substr(0, colon), value = line.substr(colon + 1);
    hw_trim(&key);
    hw_trim(&value);
    active = true;
    if (key == "level") current.level = std::atoi(value.c_str());
    else if (key == "type") current.type = value;
    else if (key == "size") current.size = value;
    else if (key == "coherency_line_size") current.line_bytes = std::atoi(value.c_str());
    else if (key == "ways_of_associativity") current.ways = std::atoi(value.c_str());
    else if (key == "shared_cpu_list") current.shared_cpus = value;
  }
  finish();
  std::sort(out.begin(), out.end(), [](const CpuCacheInfo& a, const CpuCacheInfo& b) {
    return std::tie(a.level, a.type, a.size) < std::tie(b.level, b.type, b.size);
  });
  out.erase(std::unique(out.begin(), out.end(), [](const CpuCacheInfo& a, const CpuCacheInfo& b) {
              return a.level == b.level && a.type == b.type && a.size == b.size && a.line_bytes == b.line_bytes &&
                     a.ways == b.ways && a.shared_cpus == b.shared_cpus;
            }),
            out.end());
  return out;
}

std::string format_bandwidth_gbs(double bytes_per_second) {
  if (bytes_per_second <= 0) return "--";
  char out[32];
  std::snprintf(out, sizeof out, "%.1f GB/s", bytes_per_second / 1e9);
  return out;
}

std::vector<MemoryModule> parse_memory_devices(const std::string& text) {
  std::vector<MemoryModule> out;
  MemoryModule current;
  bool in_device = false, installed = false;
  auto finish = [&] {
    if (in_device && installed && current.size_mb > 0) out.push_back(current);
    current = {};
    installed = false;
  };
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    std::string header = line;
    hw_trim(&header);
    if (header == "Memory Device") {
      finish();
      in_device = true;
      continue;
    }
    if (!in_device) continue;
    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string key = line.substr(0, colon), value = line.substr(colon + 1);
    hw_trim(&key);
    hw_trim(&value);
    if (key == "Size") {
      if (value.rfind("No Module", 0) == 0) installed = false;
      else { current.size_mb = std::atoll(value.c_str()) * (value.find("GB") != std::string::npos ? 1024 : 1); installed = true; }
    } else if (key == "Locator") current.locator = value;
    else if (key == "Manufacturer") current.manufacturer = value;
    else if (key == "Part Number") current.part_number = value;
    else if (key == "Type") current.type = value;
    else if (key == "Speed") current.speed_mhz = std::atoi(value.c_str());
  }
  finish();
  return out;
}

std::vector<DriveInfo> parse_drive_inventory(const std::string& text) {
  std::vector<DriveInfo> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream row(line);
    DriveInfo d;
    std::getline(row, d.name, '\t');
    std::string size;
    std::getline(row, size, '\t');
    d.size_bytes = std::atoll(size.c_str());
    std::getline(row, d.model, '\t');
    std::getline(row, d.vendor, '\t');
    std::getline(row, d.serial, '\t');
    std::getline(row, d.transport, '\t');
    if (!d.name.empty()) out.push_back(std::move(d));
  }
  return out;
}

std::vector<CpuTimes> parse_proc_stat_cores(const std::string& text) {
  std::vector<CpuTimes> cores;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (line.compare(0, 3, "cpu") != 0 || line.size() < 4 || line[3] < '0' || line[3] > '9') continue;
    unsigned long long v[8] = {};
    int n = std::sscanf(line.c_str(), "cpu%*d %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4],
                        &v[5], &v[6], &v[7]);
    if (n < 4) continue;
    CpuTimes t;
    for (int i = 0; i < 8; ++i) t.total += v[i];
    t.busy = t.total - v[3] - v[4];  // not idle, not iowait
    cores.push_back(t);
  }
  return cores;
}

std::vector<int> core_percents(const std::vector<CpuTimes>& prev, const std::vector<CpuTimes>& cur) {
  std::vector<int> out(cur.size(), -1);
  for (size_t i = 0; i < cur.size() && i < prev.size(); ++i) {
    if (cur[i].total <= prev[i].total) continue;
    const double td = static_cast<double>(cur[i].total - prev[i].total);
    const double bd = static_cast<double>(cur[i].busy >= prev[i].busy ? cur[i].busy - prev[i].busy : 0);
    out[i] = static_cast<int>(std::min(100.0, 100.0 * bd / td) + 0.5);
  }
  return out;
}

bool parse_meminfo(const std::string& text, MemDetail* m) {
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    unsigned long long v = 0;
    if (std::sscanf(line.c_str(), "MemTotal: %llu", &v) == 1) m->total = v;
    else if (std::sscanf(line.c_str(), "MemFree: %llu", &v) == 1) m->free = v;
    else if (std::sscanf(line.c_str(), "MemAvailable: %llu", &v) == 1) m->available = v;
    else if (std::sscanf(line.c_str(), "Cached: %llu", &v) == 1) m->cached = v;
    else if (std::sscanf(line.c_str(), "Buffers: %llu", &v) == 1) m->buffers = v;
    else if (std::sscanf(line.c_str(), "SwapTotal: %llu", &v) == 1) m->swap_total = v;
    else if (std::sscanf(line.c_str(), "SwapFree: %llu", &v) == 1) m->swap_free = v;
  }
  return m->total > 0;
}

bool is_whole_disk(const std::string& n) {
  auto digits_only = [](const std::string& s, size_t from) {
    if (from >= s.size()) return false;
    for (size_t i = from; i < s.size(); ++i)
      if (s[i] < '0' || s[i] > '9') return false;
    return true;
  };
  if (n.compare(0, 4, "nvme") == 0) {  // nvme0n1 yes, nvme0n1p2 no
    const size_t nn = n.find('n', 4);
    return nn != std::string::npos && digits_only(n, nn + 1);
  }
  if (n.compare(0, 6, "mmcblk") == 0) return digits_only(n, 6);  // mmcblk0 yes, mmcblk0p1 no
  if (n.size() >= 3 && (n.compare(0, 2, "sd") == 0 || n.compare(0, 2, "vd") == 0 || n.compare(0, 2, "xvd") == 0)) {
    for (size_t i = 2; i < n.size(); ++i)
      if (n[i] < 'a' || n[i] > 'z') return false;  // sda yes, sda1 no
    return true;
  }
  return false;
}

DiskCounters parse_diskstats(const std::string& text) {
  DiskCounters c;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    int major = 0, minor = 0;
    char name[64] = {};
    unsigned long long rd_ios, rd_merges, rd_sectors, rd_ms, wr_ios, wr_merges, wr_sectors;
    if (std::sscanf(line.c_str(), "%d %d %63s %llu %llu %llu %llu %llu %llu %llu", &major, &minor, name, &rd_ios,
                    &rd_merges, &rd_sectors, &rd_ms, &wr_ios, &wr_merges, &wr_sectors) != 10)
      continue;
    if (!is_whole_disk(name)) continue;
    c.read_bytes += rd_sectors * 512;  // diskstats sectors are always 512 bytes
    c.write_bytes += wr_sectors * 512;
  }
  return c;
}

std::string format_rate(double bps) {
  char b[32];
  if (bps >= 1e9) std::snprintf(b, sizeof b, "%.2f GB/s", bps / 1e9);
  else if (bps >= 1e6) std::snprintf(b, sizeof b, "%.1f MB/s", bps / 1e6);
  else if (bps >= 1e3) std::snprintf(b, sizeof b, "%.0f KB/s", bps / 1e3);
  else std::snprintf(b, sizeof b, "%.0f B/s", bps);
  return b;
}

double battery_power_watts(const std::string& supply_dir) {
  const std::string base = supply_dir.empty() ? std::string() : supply_dir;
  std::string bat;
  if (DIR* d = opendir(base.c_str())) {
    while (const dirent* e = readdir(d))
      if (std::strncmp(e->d_name, "BAT", 3) == 0 && (bat.empty() || base + "/" + e->d_name < bat))
        bat = base + "/" + e->d_name;
    closedir(d);
  }
  if (bat.empty()) return -1;
  const long long p = read_ll(bat + "/power_now");  // microwatts
  if (p > 0) return static_cast<double>(p) / 1e6;
  const long long i = read_ll(bat + "/current_now"), v = read_ll(bat + "/voltage_now");  // uA, uV
  if (i > 0 && v > 0) return static_cast<double>(i) * static_cast<double>(v) / 1e12;
  return -1;
}

long long rapl_energy_uj(const std::string& powercap_dir) {
  return read_ll(powercap_dir + "/intel-rapl:0/energy_uj");
}

double timer_latency_us(int tries) {
  double best = -1;
  for (int i = 0; i < tries; ++i) {
    timespec t0{}, t1{}, ask{0, 200000};
    clock_gettime(CLOCK_MONOTONIC, &t0);
    nanosleep(&ask, nullptr);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double late = (static_cast<double>(t1.tv_sec - t0.tv_sec) * 1e9 + static_cast<double>(t1.tv_nsec - t0.tv_nsec) - 200000.0) / 1e3;
    if (best < 0 || late < best) best = late;
  }
  return std::max(0.0, best);
}

GpuDetail read_amd_gpu_detail(const std::string& dev) {
  GpuDetail g;
  g.percent = static_cast<int>(read_ll(dev + "/gpu_busy_percent"));
  g.vram_used = read_ll(dev + "/mem_info_vram_used");
  g.vram_total = read_ll(dev + "/mem_info_vram_total");
  const std::string hw = first_hwmon(dev);
  if (!hw.empty()) {
    long long p = read_ll(hw + "/power1_average");
    if (p <= 0) p = read_ll(hw + "/power1_input");
    if (p > 0) g.power_w = static_cast<double>(p) / 1e6;
    const long long sclk = read_ll(hw + "/freq1_input"), mclk = read_ll(hw + "/freq2_input");  // Hz
    if (sclk > 0) g.core_mhz = static_cast<int>(sclk / 1000000);
    if (mclk > 0) g.mem_mhz = static_cast<int>(mclk / 1000000);
  }
  return g;
}

std::vector<GpuDetail> parse_nvidia_smi_csv(const std::string& text) {
  std::vector<GpuDetail> cards;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    std::vector<double> f;
    std::istringstream fields(line);
    std::string tok;
    while (std::getline(fields, tok, ',')) {
      char* end = nullptr;
      const double v = std::strtod(tok.c_str(), &end);
      f.push_back(end == tok.c_str() ? -1.0 : v);  // "[N/A]" and blanks
    }
    if (f.empty()) continue;
    GpuDetail g;
    g.percent = f[0] >= 0 ? static_cast<int>(std::min(100.0, f[0])) : -1;
    if (f.size() > 1) g.power_w = f[1];
    if (f.size() > 2 && f[2] >= 0) g.core_mhz = static_cast<int>(f[2]);
    if (f.size() > 3 && f[3] >= 0) g.mem_mhz = static_cast<int>(f[3]);
    if (f.size() > 4 && f[4] >= 0) g.vram_used = static_cast<long long>(f[4] * 1048576.0);
    if (f.size() > 5 && f[5] >= 0) g.vram_total = static_cast<long long>(f[5] * 1048576.0);
    cards.push_back(g);
  }
  return cards;
}

}  // namespace fleetwm
