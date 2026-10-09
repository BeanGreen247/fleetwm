// Task Manager data: /proc parsers on fixture text, rates from two samples, sorting, history, and one live read of /proc.
#include <gtest/gtest.h>

#include <unistd.h>

#include "proc_stats.hpp"

using namespace fleetwm;

namespace {
const char* kStat = "4242 (my (weird) app) S 1 4242 4242 0 -1 4194560 100 0 0 0 150 50 0 0 20 0 7 0 12345 1000000 2500 18446744073709551615 1 1 0 0 0 0 0 0 0 0 0 0 17 3 0 0 0 0 0\n";
ProcInfo proc(int pid, const char* name, unsigned long long ticks, long long rss, unsigned long long rd = 0, unsigned long long wr = 0, unsigned uid = 1000) {
  ProcInfo p;
  p.pid = pid;
  p.name = name;
  p.cpu_ticks = ticks;
  p.rss_kb = rss;
  p.read_bytes = rd;
  p.write_bytes = wr;
  p.uid = uid;
  return p;
}
}  // namespace

TEST(ProcStats, StatLineWithParenthesesInTheName) {
  ProcInfo p;
  ASSERT_TRUE(parse_pid_stat(kStat, 4, &p));
  EXPECT_EQ(p.pid, 4242);
  EXPECT_EQ(p.name, "my (weird) app");
  EXPECT_EQ(p.state, 'S');
  EXPECT_EQ(p.cpu_ticks, 200u);  // utime 150 + stime 50
  EXPECT_EQ(p.threads, 7);
  EXPECT_EQ(p.rss_kb, 2500 * 4);
}

TEST(ProcStats, BadStatLinesAreRejected) {
  ProcInfo p;
  EXPECT_FALSE(parse_pid_stat("", 4, &p));
  EXPECT_FALSE(parse_pid_stat("12 no parentheses", 4, &p));
  EXPECT_FALSE(parse_pid_stat("0 (x) S 1", 4, &p));          // pid 0
  EXPECT_FALSE(parse_pid_stat("12 (x) S 1 2 3", 4, &p));     // too few fields
}

TEST(ProcStats, IoFile) {
  unsigned long long r = 0, w = 0;
  ASSERT_TRUE(parse_pid_io("rchar: 100\nwchar: 50\nsyscr: 1\nsyscw: 1\nread_bytes: 4096\nwrite_bytes: 8192\ncancelled_write_bytes: 0\n", &r, &w));
  EXPECT_EQ(r, 4096u);
  EXPECT_EQ(w, 8192u);
  EXPECT_FALSE(parse_pid_io("rchar: 1\n", &r, &w));
}

TEST(ProcStats, TotalCpuLine) {
  CpuTimes t;
  ASSERT_TRUE(parse_proc_stat_total("cpu  100 0 50 800 50 0 0 0 0 0\ncpu0 50 0 25 400 25 0 0 0 0 0\n", &t));
  EXPECT_EQ(t.total, 1000u);
  EXPECT_EQ(t.busy, 150u);  // total minus idle and iowait
  EXPECT_FALSE(parse_proc_stat_total("nothing", &t));
}

TEST(ProcStats, NetDevSumsEverythingButLoopback) {
  const char* text =
      "Inter-|   Receive                                                |  Transmit\n"
      " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
      "    lo: 9999 10 0 0 0 0 0 0 9999 10 0 0 0 0 0 0\n"
      "  eth0: 1000 10 0 0 0 0 0 0 500 5 0 0 0 0 0 0\n"
      " wlan0: 2000 20 0 0 0 0 0 0 250 2 0 0 0 0 0 0\n";
  const NetCounters n = parse_net_dev(text);
  EXPECT_EQ(n.rx_bytes, 3000u);
  EXPECT_EQ(n.tx_bytes, 750u);
}

TEST(ProcStats, SizeFormatting) {
  EXPECT_EQ(format_kib(0), "0 KB");
  EXPECT_EQ(format_kib(-5), "0 KB");
  EXPECT_EQ(format_kib(512), "512 KB");
  EXPECT_EQ(format_kib(2048), "2 MB");
  EXPECT_EQ(format_kib(1024 * 1024), "1.0 GB");
  EXPECT_EQ(format_kib(1536 * 1024), "1.5 GB");
}

TEST(ProcStats, RatesFromTwoSamples) {
  ProcessTable t;
  t.apply({proc(1, "a", 100, 1000, 0, 0), proc(2, "b", 0, 500)}, 1.0, 100, 4);
  ASSERT_EQ(t.rows().size(), 2u);
  EXPECT_TRUE(t.rows()[0].fresh);  // no rate on the first sample
  EXPECT_EQ(t.rows()[0].cpu_percent, 0);
  // One second later, `a` used 200 more ticks of 100 Hz = two full cores of four = 50% of the machine; it read 1 MB.
  t.apply({proc(1, "a", 300, 1000, 1000000, 0), proc(2, "b", 0, 500), proc(3, "new", 5, 10)}, 1.0, 100, 4);
  ASSERT_EQ(t.rows().size(), 3u);
  EXPECT_FALSE(t.rows()[0].fresh);
  EXPECT_NEAR(t.rows()[0].cpu_percent, 50.0, 1e-9);
  EXPECT_NEAR(t.rows()[0].disk_bps, 1000000.0, 1e-6);
  EXPECT_EQ(t.rows()[1].cpu_percent, 0);
  EXPECT_TRUE(t.rows()[2].fresh);
}

TEST(ProcStats, CounterGoingBackwardsIsTreatedAsNew) {
  ProcessTable t;
  t.apply({proc(7, "x", 500, 1)}, 1.0, 100, 1);
  t.apply({proc(7, "x", 10, 1)}, 1.0, 100, 1);  // the pid was reused by another program
  EXPECT_TRUE(t.rows()[0].fresh);
  EXPECT_EQ(t.rows()[0].cpu_percent, 0);
}

TEST(ProcStats, SortByEachColumnAndTieBreak) {
  std::vector<ProcRow> rows(3);
  rows[0].info = proc(30, "zed", 0, 100);
  rows[0].cpu_percent = 5;
  rows[1].info = proc(10, "alpha", 0, 300);
  rows[1].cpu_percent = 5;
  rows[2].info = proc(20, "mid", 0, 200, 0, 0, 0);
  rows[2].cpu_percent = 50;
  const std::unordered_map<unsigned, std::string> users = {{0, "root"}, {1000, "bean"}};
  sort_rows(&rows, ProcColumn::Cpu, true, users);
  EXPECT_EQ(rows[0].info.name, "mid");
  EXPECT_EQ(rows[1].info.name, "alpha");  // equal cpu: by name
  sort_rows(&rows, ProcColumn::Memory, true, users);
  EXPECT_EQ(rows[0].info.name, "alpha");
  sort_rows(&rows, ProcColumn::Memory, false, users);
  EXPECT_EQ(rows[0].info.name, "zed");
  sort_rows(&rows, ProcColumn::Pid, false, users);
  EXPECT_EQ(rows[0].info.pid, 10);
  sort_rows(&rows, ProcColumn::User, false, users);
  EXPECT_EQ(rows[0].info.name, "alpha");  // bean < root, ties by name
  sort_rows(&rows, ProcColumn::Name, false, users);
  EXPECT_EQ(rows[0].info.name, "alpha");
}

TEST(ProcStats, HistoryKeepsTheLastSamples) {
  History h(3);
  for (double v : {1.0, 2.0, 3.0, 4.0}) h.push(v);
  EXPECT_EQ(h.values(), (std::vector<double>{2, 3, 4}));
  EXPECT_EQ(h.max(), 4);
}

TEST(ProcStats, AudioLatencyFromPwMetadata) {
  const char* text =
      "Found \"settings\" metadata 32\n"
      "update: id:0 key:'log.level' value:'2' type:''\n"
      "update: id:0 key:'clock.rate' value:'48000' type:''\n"
      "update: id:0 key:'clock.quantum' value:'1024' type:''\n";
  EXPECT_NEAR(parse_audio_latency_ms(text), 21.333, 0.01);
  EXPECT_EQ(parse_audio_latency_ms("update: id:0 key:'clock.rate' value:'48000' type:''\n"), -1);
  EXPECT_EQ(parse_audio_latency_ms(""), -1);
}

TEST(ProcStats, LiveReadFindsThisProcess) {
  ProcessTable t;
  ASSERT_TRUE(t.refresh(1.0, sysconf(_SC_CLK_TCK), 1));
  bool found = false;
  for (const ProcRow& r : t.rows())
    if (r.info.pid == getpid()) {
      found = true;
      EXPECT_GT(r.info.rss_kb, 0);
      EXPECT_EQ(r.info.uid, getuid());
    }
  EXPECT_TRUE(found);
  EXPECT_GT(t.rows().size(), 3u);
  ASSERT_TRUE(t.refresh(1.0, sysconf(_SC_CLK_TCK), 1));  // second sample reuses the open files
  EXPECT_GT(t.rows().size(), 3u);
}
