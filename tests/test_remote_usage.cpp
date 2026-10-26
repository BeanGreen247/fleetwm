#include <gtest/gtest.h>

#include <cstdlib>

#include "remote_usage.hpp"
#include "test_util.hpp"

using namespace fleetwm;

TEST(RemoteLiveStats, RoundTripsThroughTheFile) {
  RemoteLive live;
  live.proto[kProtoVnc] = {2, 1000, 50, 340.0, 12.0, 4.5, 2048};
  live.proto[kProtoRdp] = {0, 7, 8, 0, 0, 0, 0};
  const std::string text = format_remote_live(live, 1000);
  RemoteLive back;
  time_t written = 0;
  ASSERT_TRUE(parse_remote_live(text, &back, &written));
  EXPECT_EQ(written, 1000);
  EXPECT_EQ(back.proto[kProtoVnc].clients, 2);
  EXPECT_EQ(back.proto[kProtoVnc].tx, 1000u);
  EXPECT_DOUBLE_EQ(back.proto[kProtoVnc].tx_rate, 340.0);
  EXPECT_NEAR(back.proto[kProtoVnc].cpu_percent, 4.5, 0.05);
  EXPECT_EQ(back.proto[kProtoRdp].rx, 8u);
  EXPECT_EQ(back.total_clients(), 2);
  EXPECT_FALSE(parse_remote_live("nonsense", &back, &written));
}

TEST(RemoteLiveStats, LabelNamesTheProtocolsInUse) {
  RemoteLive live;
  EXPECT_EQ(remote_live_label(live), "");
  live.proto[kProtoVnc].clients = 1;
  EXPECT_EQ(remote_live_label(live), "VNC");
  live.proto[kProtoRdp].clients = 1;
  EXPECT_EQ(remote_live_label(live), "VNC + RDP");
  live.proto[kProtoVnc].clients = 0;
  EXPECT_EQ(remote_live_label(live), "RDP");
}

TEST(RemoteLiveStats, TooltipListsOnlyConnectedProtocols) {
  RemoteLive live;
  live.proto[kProtoRdp] = {1, 5 * 1024 * 1024, 2048, 150 * 1024.0, 3 * 1024.0, 2.25, 10 * 1024 * 1024};
  const std::string t = remote_live_tooltip(live);
  EXPECT_NE(t.find("RDP: 1 viewer"), std::string::npos);
  EXPECT_EQ(t.find("VNC:"), std::string::npos);
  EXPECT_NE(t.find("Sent 5.0 MB, received 2 KB"), std::string::npos);
  EXPECT_NE(t.find("up 150 KB/s"), std::string::npos);
  EXPECT_NE(t.find("CPU 2.2%"), std::string::npos);
  EXPECT_NE(t.find("memory 10.0 MB"), std::string::npos);
}

TEST(RemoteFormat, BytesRatesAndDurations) {
  EXPECT_EQ(remote_format_bytes(0), "0 B");
  EXPECT_EQ(remote_format_bytes(2048), "2 KB");
  EXPECT_EQ(remote_format_bytes(1.5 * 1024 * 1024), "1.5 MB");
  EXPECT_EQ(remote_format_bytes(3.0 * 1024 * 1024 * 1024), "3.00 GB");
  EXPECT_EQ(remote_format_rate(1024), "1 KB/s");
  EXPECT_EQ(remote_format_duration(45), "45 s");
  EXPECT_EQ(remote_format_duration(720), "12 min");
  EXPECT_EQ(remote_format_duration(3 * 3600 + 300), "3 h 05 min");
}

namespace {
struct ScopedState {
  ScopedState() {
    dir = std::string(::testing::TempDir()) + "remote-usage-test";
    setenv("XDG_STATE_HOME", dir.c_str(), 1);
  }
  ~ScopedState() { unsetenv("XDG_STATE_HOME"); }
  std::string dir;
};
}  // namespace

TEST(RemoteUsage, AveragesOverTheLastNDaysIncludingEmptyOnes) {
  UsageHistory h;
  const time_t now = 1760000000;  // any day
  add_usage(&h, usage_day_key(now), kProtoVnc, {700, 70, 3600, 2000});
  add_usage(&h, usage_day_key(now - 86400 * 3), kProtoVnc, {1400, 0, 0, 0});
  add_usage(&h, usage_day_key(now - 86400 * 40), kProtoVnc, {99999, 0, 0, 0});  // outside every window below
  const UsageAverage one = average_usage(h, kProtoVnc, 1, now);
  EXPECT_DOUBLE_EQ(one.tx, 700);
  EXPECT_DOUBLE_EQ(one.active_s, 3600);
  EXPECT_DOUBLE_EQ(one.cpu_s, 2);
  const UsageAverage week = average_usage(h, kProtoVnc, 7, now);
  EXPECT_DOUBLE_EQ(week.tx, (700 + 1400) / 7.0);
  const UsageAverage month = average_usage(h, kProtoVnc, 28, now);
  EXPECT_DOUBLE_EQ(month.tx, (700 + 1400) / 28.0);
  EXPECT_DOUBLE_EQ(average_usage(h, kProtoRdp, 7, now).tx, 0);
  EXPECT_EQ(h.all_time[kProtoVnc].tx, 700u + 1400u + 99999u);
}

TEST(RemoteUsage, SavesAndLoadsAndKeepsSixtyDays) {
  ScopedState state;
  UsageHistory h;
  const time_t now = 1760000000;
  for (int i = 0; i < 70; ++i) add_usage(&h, usage_day_key(now - 86400 * i), kProtoRdp, {1, 2, 3, 4});
  EXPECT_EQ(h.days.size(), static_cast<size_t>(kUsageKeepDays));
  EXPECT_EQ(h.all_time[kProtoRdp].tx, 70u);  // the running total keeps what the daily list forgot
  save_usage(h);
  const UsageHistory back = load_usage();
  EXPECT_EQ(back.days.size(), h.days.size());
  EXPECT_EQ(back.all_time[kProtoRdp].rx, 140u);
  EXPECT_EQ(back.days.at(usage_day_key(now))[kProtoRdp].active_s, 3u);
}
