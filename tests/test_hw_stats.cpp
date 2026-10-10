#include "hw_stats.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <filesystem>
#include <fstream>

using namespace fleetwm;
namespace fs = std::filesystem;

namespace {
void put(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << s;
}
fs::path scratch(const char* name) {
  fs::path d = fs::temp_directory_path() / (std::string("fleetwm-hw-") + name + std::to_string(getpid()));
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}
}  // namespace

TEST(HwStats, ProcStatCoresSkipAggregateLine) {
  const std::string s =
      "cpu  100 0 100 800 0 0 0 0 0 0\n"
      "cpu0 50 0 50 400 0 0 0 0 0 0\n"
      "cpu1 50 0 50 400 0 0 0 0 0 0\n"
      "intr 1 2 3\n";
  auto c = parse_proc_stat_cores(s);
  ASSERT_EQ(c.size(), 2u);
  EXPECT_EQ(c[0].total, 500u);
  EXPECT_EQ(c[0].busy, 100u);
}

TEST(HwStats, MachineInventoryParsers) {
  const CpuInfo cpu = parse_cpuinfo("processor : 0\nmodel name : Old Celeron\ncpu MHz : 798.4\ncpu cores : 2\nprocessor : 1\n");
  EXPECT_EQ(cpu.model, "Old Celeron");
  EXPECT_EQ(cpu.logical_threads, 2);
  EXPECT_EQ(cpu.physical_cores, 2);
  EXPECT_EQ(cpu.mhz, 798);
  const auto ram = parse_memory_devices("Memory Device\n\tSize: 4 GB\n\tLocator: DIMM 0\n\tManufacturer: Acme\n\tPart Number: SO-DIMM\n\tType: DDR4\n\tSpeed: 2666 MT/s\n\nMemory Device\n\tSize: No Module Installed\n");
  ASSERT_EQ(ram.size(), 1u);
  EXPECT_EQ(ram[0].size_mb, 4096);
  EXPECT_EQ(ram[0].speed_mhz, 2666);
  const auto drives = parse_drive_inventory("sda\t1000000\tSSD 500GB\tAcme\tABC123\tsata\n");
  ASSERT_EQ(drives.size(), 1u);
  EXPECT_EQ(drives[0].model, "SSD 500GB");
  EXPECT_EQ(drives[0].size_bytes, 1000000);
}

TEST(HwStats, CorePercentsFromTwoSamples) {
  std::vector<CpuTimes> a{{0, 0}, {10, 100}}, b{{50, 100}, {10, 200}};
  auto p = core_percents(a, b);
  ASSERT_EQ(p.size(), 2u);
  EXPECT_EQ(p[0], 50);
  EXPECT_EQ(p[1], 0);
  EXPECT_EQ(core_percents(b, b)[0], -1);  // no time passed
  EXPECT_EQ(core_percents({}, b)[0], -1);  // no baseline
}

TEST(HwStats, MemInfoFields) {
  MemDetail m;
  ASSERT_TRUE(parse_meminfo("MemTotal: 2000 kB\nMemFree: 300 kB\nMemAvailable: 900 kB\nBuffers: 50 kB\nCached: 400 kB\n"
                            "SwapTotal: 100 kB\nSwapFree: 60 kB\n", &m));
  EXPECT_EQ(m.total, 2000u);
  EXPECT_EQ(m.free, 300u);
  EXPECT_EQ(m.available, 900u);
  EXPECT_EQ(m.buffers, 50u);
  EXPECT_EQ(m.cached, 400u);
  EXPECT_EQ(m.swap_total - m.swap_free, 40u);
  MemDetail none;
  EXPECT_FALSE(parse_meminfo("", &none));
}

TEST(HwStats, WholeDiskNames) {
  for (const char* n : {"sda", "sdb", "vda", "nvme0n1", "mmcblk0", "sdaa"}) EXPECT_TRUE(is_whole_disk(n)) << n;
  for (const char* n : {"sda1", "vda2", "nvme0n1p2", "mmcblk0p1", "loop0", "dm-0", "ram0", "sr0", "zram0", "nvme0"})
    EXPECT_FALSE(is_whole_disk(n)) << n;
}

TEST(HwStats, DiskstatsCountsWholeDisksOnly) {
  const std::string s =
      "   8       0 sda 100 0 2000 0 50 0 1000 0 0 0 0\n"
      "   8       1 sda1 90 0 1900 0 40 0 900 0 0 0 0\n"
      " 259       0 nvme0n1 10 0 100 0 20 0 300 0 0 0 0\n"
      " 259       1 nvme0n1p1 10 0 100 0 20 0 300 0 0 0 0\n"
      " 253       0 dm-0 10 0 999 0 20 0 999 0 0 0 0\n"
      "   7       0 loop0 1 0 999 0 1 0 999 0 0 0 0\n";
  auto c = parse_diskstats(s);
  EXPECT_EQ(c.read_bytes, (2000u + 100u) * 512);
  EXPECT_EQ(c.write_bytes, (1000u + 300u) * 512);
}

TEST(HwStats, FormatRate) {
  EXPECT_EQ(format_rate(0), "0 B/s");
  EXPECT_EQ(format_rate(312000), "312 KB/s");
  EXPECT_EQ(format_rate(1.4e6), "1.4 MB/s");
  EXPECT_EQ(format_rate(2.5e9), "2.50 GB/s");
}

TEST(HwStats, BatteryPowerFromPowerNowOrCurrentTimesVoltage) {
  auto d = scratch("bat");
  put(d / "BAT0/power_now", "7500000\n");
  EXPECT_DOUBLE_EQ(battery_power_watts(d.string()), 7.5);
  fs::remove(d / "BAT0/power_now");
  put(d / "BAT0/current_now", "1000000\n");
  put(d / "BAT0/voltage_now", "12000000\n");
  EXPECT_DOUBLE_EQ(battery_power_watts(d.string()), 12.0);
  EXPECT_EQ(battery_power_watts((d / "none").string()), -1);
  fs::remove_all(d);
}

TEST(HwStats, RaplEnergy) {
  auto d = scratch("rapl");
  put(d / "intel-rapl:0/energy_uj", "123456\n");
  EXPECT_EQ(rapl_energy_uj(d.string()), 123456);
  EXPECT_EQ(rapl_energy_uj((d / "none").string()), -1);
  fs::remove_all(d);
}

TEST(HwStats, TimerLatencyIsSmallAndNonNegative) {
  const double us = timer_latency_us(3);
  EXPECT_GE(us, 0.0);
  EXPECT_LT(us, 50000.0);
}

TEST(HwStats, AmdDetail) {
  auto d = scratch("amd");
  put(d / "gpu_busy_percent", "37\n");
  put(d / "mem_info_vram_used", "1073741824\n");
  put(d / "mem_info_vram_total", "8589934592\n");
  put(d / "hwmon/hwmon3/power1_average", "45000000\n");
  put(d / "hwmon/hwmon3/freq1_input", "1800000000\n");
  put(d / "hwmon/hwmon3/freq2_input", "875000000\n");
  GpuDetail g = read_amd_gpu_detail(d.string());
  EXPECT_EQ(g.percent, 37);
  EXPECT_EQ(g.vram_used, 1073741824);
  EXPECT_EQ(g.vram_total, 8589934592);
  EXPECT_DOUBLE_EQ(g.power_w, 45.0);
  EXPECT_EQ(g.core_mhz, 1800);
  EXPECT_EQ(g.mem_mhz, 875);
  GpuDetail none = read_amd_gpu_detail((d / "x").string());
  EXPECT_EQ(none.percent, -1);
  EXPECT_EQ(none.power_w, -1);
  fs::remove_all(d);
}

TEST(HwStats, NvidiaSmiCsv) {
  auto c = parse_nvidia_smi_csv("12, 45.20, 1500, 5000, 1024, 8192\n0, [N/A], 300, 405, 10, 4096\n\n");
  ASSERT_EQ(c.size(), 2u);
  EXPECT_EQ(c[0].percent, 12);
  EXPECT_DOUBLE_EQ(c[0].power_w, 45.2);
  EXPECT_EQ(c[0].core_mhz, 1500);
  EXPECT_EQ(c[0].mem_mhz, 5000);
  EXPECT_EQ(c[0].vram_used, 1024ll * 1048576);
  EXPECT_EQ(c[0].vram_total, 8192ll * 1048576);
  EXPECT_EQ(c[1].power_w, -1);
  EXPECT_EQ(c[1].percent, 0);
  // the old one-field output still parses
  EXPECT_EQ(parse_nvidia_smi_csv("55\n")[0].percent, 55);
}
