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

TEST(HwStats, CpuCacheInventoryAndBandwidth) {
  const auto caches = parse_cpu_cache_info(
      "level: 1\ntype: Data\nsize: 32K\ncoherency_line_size: 64\nways_of_associativity: 8\nshared_cpu_list: 0,4\n\n"
      "level: 3\ntype: Unified\nsize: 6144K\ncoherency_line_size: 64\nways_of_associativity: 12\nshared_cpu_list: 0-7\n\n"
      "level: 1\ntype: Data\nsize: 32K\ncoherency_line_size: 64\nways_of_associativity: 8\nshared_cpu_list: 0,4\n");
  ASSERT_EQ(caches.size(), 2u);
  EXPECT_EQ(caches[0].level, 1);
  EXPECT_EQ(caches[0].size, "32K");
  EXPECT_EQ(caches[1].level, 3);
  EXPECT_EQ(format_bandwidth_gbs(21.328e9), "21.3 GB/s");
  EXPECT_EQ(format_bandwidth_gbs(0), "--");
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

// ---- the extra CPU, memory, disk, pressure and GPU readings of the Task Manager ----

TEST(MemComposition, ColumnsMatchFree) {
  MemDetail m;
  m.total = 8000000, m.free = 1000000, m.available = 5000000, m.cached = 3000000, m.buffers = 200000, m.sreclaimable = 300000, m.shmem = 500000;
  const MemComposition c = mem_composition(m);
  EXPECT_EQ(c.used, 3000000u) << "free: used = total - available";
  EXPECT_EQ(c.free, 1000000u);
  EXPECT_EQ(c.shared, 500000u);
  EXPECT_EQ(c.buff_cache, 3500000u) << "free: buffers + cached + reclaimable slab, shared included";
  EXPECT_EQ(c.available, 5000000u);
  EXPECT_EQ(c.used + c.reclaimable + c.free, c.total);
}

TEST(MemComposition, NeverGoesNegativeWhenTheCounterDisagree) {
  MemDetail m;
  m.total = 1000, m.free = 900, m.available = 2000, m.cached = 5000, m.shmem = 4000;  // inconsistent on purpose
  const MemComposition c = mem_composition(m);
  EXPECT_EQ(c.used + c.reclaimable + c.free, c.total);
  EXPECT_LE(c.shared, c.total);
  EXPECT_LE(c.buff_cache, c.total);
}

TEST(MemInfo, ReadsTheExtraFields) {
  MemDetail m;
  ASSERT_TRUE(parse_meminfo("MemTotal: 3500000 kB\nMemFree: 100 kB\nShmem: 42 kB\nSReclaimable: 7 kB\nSUnreclaim: 8 kB\nAnonPages: 9 kB\nMapped: 10 kB\nDirty: 11 kB\nWriteback: 12 kB\nCommitted_AS: 13 kB\nCommitLimit: 14 kB\n", &m));
  EXPECT_EQ(m.shmem, 42u);
  EXPECT_EQ(m.sreclaimable, 7u);
  EXPECT_EQ(m.sunreclaim, 8u);
  EXPECT_EQ(m.anon, 9u);
  EXPECT_EQ(m.committed, 13u);
  EXPECT_EQ(m.commit_limit, 14u);
}

TEST(DiskActivity, SumsWholeDisksAndSkipsPartitions) {
  const char* t =
      "   8       0 sda 100 5 2000 400 50 2 1000 300 3 700 800 0 0 0 0\n"
      "   8       1 sda1 90 5 1900 390 40 2 900 290 0 600 700 0 0 0 0\n"
      " 259       0 nvme0n1 10 0 80 20 10 0 80 30 0 50 60 0 0 0 0\n"
      "   7       0 loop0 5 0 40 1 0 0 0 0 0 1 1 0 0 0 0\n";
  const DiskActivity a = parse_disk_activity(t);
  EXPECT_EQ(a.reads, 110u);
  EXPECT_EQ(a.writes, 60u);
  EXPECT_EQ(a.read_ms, 420u);
  EXPECT_EQ(a.write_ms, 330u);
  EXPECT_EQ(a.io_ms, 750u);  // 700 (sda) + 50 (nvme0n1): the whole-disk lines only
  EXPECT_EQ(a.in_flight, 3u);
}

TEST(DiskActivity, AwaitIsTimePerRequestAndBusyIsShareOfTheInterval) {
  DiskActivity p, c;
  c.reads = 8, c.writes = 2, c.read_ms = 40, c.write_ms = 60, c.io_ms = 250;
  EXPECT_DOUBLE_EQ(disk_await_ms(p, c), 10.0);  // 100 ms over 10 requests
  EXPECT_NEAR(disk_busy_percent(p, c, 1.0), 25.0, 1e-9);
  EXPECT_NEAR(disk_busy_percent(p, c, 0.5), 50.0, 1e-9);
  EXPECT_EQ(disk_await_ms(c, c), -1) << "nothing completed";
  EXPECT_EQ(disk_await_ms(c, p), -1) << "counters went backwards (a disk was removed)";
  DiskActivity busy;
  busy.io_ms = 99999;
  EXPECT_EQ(disk_busy_percent(p, busy, 1.0), 100.0);
}

TEST(CpuBreakdown, ParsesTheCpuLineAndTheCounters) {
  const char* t = "cpu  400 20 300 9000 150 10 20 5 0 0\ncpu0 200 10 150 4500 75 5 10 2 0 0\nintr 123456 1 2\nctxt 987654\nbtime 1\nprocesses 4321\nprocs_running 3\nprocs_blocked 1\n";
  CpuBreakdown b;
  ASSERT_TRUE(parse_proc_stat_breakdown(t, &b));
  EXPECT_EQ(b.user, 400u);
  EXPECT_EQ(b.iowait, 150u);
  EXPECT_EQ(b.steal, 5u);
  EXPECT_EQ(b.ctxt, 987654u);
  EXPECT_EQ(b.intr, 123456u);
  EXPECT_EQ(b.forks, 4321u);
  EXPECT_EQ(b.running, 3);
  EXPECT_EQ(b.blocked, 1);
  EXPECT_EQ(b.total(), 400u + 20 + 300 + 9000 + 150 + 10 + 20 + 5);
  EXPECT_FALSE(parse_proc_stat_breakdown("nothing", &b));
}

TEST(CpuBreakdown, RatesAreSharesOfTheIntervalAndPerSecondCounts) {
  CpuBreakdown p, c;
  c.user = 50, c.nice = 10, c.system = 20, c.idle = 100, c.iowait = 15, c.irq = 3, c.softirq = 2, c.steal = 0;
  c.ctxt = 5000, c.intr = 2000, c.forks = 30;
  const CpuRates r = cpu_rates(p, c, 2.0);
  ASSERT_TRUE(r.valid);
  EXPECT_NEAR(r.user, 100.0 * 60 / 200, 1e-9);
  EXPECT_NEAR(r.system, 10.0, 1e-9);
  EXPECT_NEAR(r.iowait, 7.5, 1e-9);
  EXPECT_NEAR(r.irq, 2.5, 1e-9);
  EXPECT_DOUBLE_EQ(r.ctxt_per_s, 2500.0);
  EXPECT_DOUBLE_EQ(r.intr_per_s, 1000.0);
  EXPECT_DOUBLE_EQ(r.forks_per_s, 15.0);
  EXPECT_FALSE(cpu_rates(c, c, 1.0).valid) << "no time passed between the samples";
  EXPECT_FALSE(cpu_rates(p, c, 0.0).valid);
}

TEST(Pressure, SomeAndFullLines) {
  Pressure p;
  ASSERT_TRUE(parse_pressure("some avg10=1.47 avg60=1.04 avg300=0.75 total=22873866\nfull avg10=0.25 avg60=0.10 avg300=0.00 total=1\n", &p));
  EXPECT_DOUBLE_EQ(p.some10, 1.47);
  EXPECT_DOUBLE_EQ(p.some60, 1.04);
  EXPECT_DOUBLE_EQ(p.full10, 0.25);
  Pressure only_some;
  ASSERT_TRUE(parse_pressure("some avg10=0.00 avg60=0.02 avg300=0.13 total=5\n", &only_some));  // the cpu file of older kernels has no full line
  EXPECT_EQ(only_some.full10, -1);
  EXPECT_FALSE(parse_pressure("", &only_some));
}

TEST(AmdDpm, ActiveAndHighestLevel) {
  int cur = 0, max = 0;
  ASSERT_TRUE(parse_pp_dpm("0: 300Mhz\n1: 1200Mhz *\n2: 2100Mhz\n", &cur, &max));
  EXPECT_EQ(cur, 1200);
  EXPECT_EQ(max, 2100);
  ASSERT_TRUE(parse_pp_dpm("0: 96Mhz\n1: 456Mhz\n", &cur, &max));
  EXPECT_EQ(cur, -1) << "no level is marked";
  EXPECT_EQ(max, 456);
  EXPECT_FALSE(parse_pp_dpm("", &cur, &max));
}

TEST(NvidiaCsv, MemoryUtilisationAndMaxClocksWhenPresent) {
  auto c = parse_nvidia_smi_csv("30, 80.0, 1400, 7000, 2048, 8192, 22, 2100, 7500\n");
  ASSERT_EQ(c.size(), 1u);
  EXPECT_EQ(c[0].mem_busy_percent, 22);
  EXPECT_EQ(c[0].core_max_mhz, 2100);
  EXPECT_EQ(c[0].mem_max_mhz, 7500);
  EXPECT_EQ(parse_nvidia_smi_csv("30, 80.0, 1400, 7000, 2048, 8192\n")[0].mem_busy_percent, -1) << "the six-field form still works";
}

TEST(ImcBandwidth, ParsesThePmuScale) {
  double s = 0;
  ASSERT_TRUE(parse_pmu_scale("6.103515625e-5\n", &s));
  EXPECT_DOUBLE_EQ(s, 6.103515625e-5);
  EXPECT_FALSE(parse_pmu_scale("", &s));
  EXPECT_FALSE(parse_pmu_scale("0", &s));
  EXPECT_FALSE(parse_pmu_scale("abc", &s));
}

TEST(ImcBandwidth, SaysWhyItIsNotAvailable) {
  const fs::path d = scratch("imc");
  ImcBandwidth none;
  none.open((d / "absent").string());
  EXPECT_FALSE(none.ok());
  EXPECT_NE(none.status().find("no memory controller"), std::string::npos);

  put(d / "typeonly/type", "99999\n");
  ImcBandwidth bare;
  bare.open((d / "typeonly").string());
  EXPECT_FALSE(bare.ok());
  EXPECT_NE(bare.status().find("not described"), std::string::npos);

  put(d / "full/type", "99999\n");
  put(d / "full/events/data_reads", "event=0x01\n");
  put(d / "full/events/data_writes", "event=0x02\n");
  put(d / "full/events/data_reads.scale", "6.103515625e-5\n");
  put(d / "full/events/data_writes.scale", "6.103515625e-5\n");
  ImcBandwidth full;
  full.open((d / "full").string());  // the kernel has no PMU of that type: it must fail cleanly, never crash
  EXPECT_FALSE(full.ok());
  full.sample();
  EXPECT_LT(full.read_mib_s(), 0);
  fs::remove_all(d);
}
