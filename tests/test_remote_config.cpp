#include <gtest/gtest.h>

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <set>

#include "remote_config.hpp"
#include "test_util.hpp"

using namespace fleetwm;

using RemoteConfigTest = testutil::ScopedConfigHome;

TEST_F(RemoteConfigTest, BothServersOffByDefault) {
  const RemoteConfig c = load_remote_config();
  EXPECT_FALSE(c.vnc_enabled);
  EXPECT_FALSE(c.rdp_enabled);
  EXPECT_EQ(c.vnc_port, 5900);
  EXPECT_EQ(c.rdp_port, 3389);
}

TEST_F(RemoteConfigTest, RoundTripsAndIsPrivate) {
  RemoteConfig c;
  c.vnc_enabled = true;
  c.vnc_port = 5911;
  c.vnc_password = "abcd2345";
  c.rdp_enabled = true;
  c.rdp_password = "longer-secret";
  c.vnc_tls = true;
  c.vnc_quality = 40;
  c.vnc_max_kbps = 256;
  c.rdp_tls = false;
  c.rdp_quality = 70;
  c.rdp_max_kbps = 1024;
  save_remote_config(c);
  EXPECT_EQ(load_remote_config(), c);
  struct stat st;
  ASSERT_EQ(stat(remote_user_config_path().c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0600u);  // it holds the passwords
}

TEST_F(RemoteConfigTest, PortsAreClamped) {
  write_config("remote.toml", "[vnc]\nport = 99999\n[rdp]\nport = 0\n");
  const RemoteConfig c = load_remote_config();
  EXPECT_EQ(c.vnc_port, 65535);
  EXPECT_EQ(c.rdp_port, 1);
}

TEST_F(RemoteConfigTest, BrokenFileSwitchesEverythingOff) {
  write_config("remote.toml", "[vnc\nenabled = true");
  EXPECT_FALSE(load_remote_config().vnc_enabled);
}

TEST_F(RemoteConfigTest, TransportAndLimitDefaults) {
  const RemoteConfig c = load_remote_config();
  EXPECT_FALSE(c.vnc_tls);  // the classic login, which every viewer speaks
  EXPECT_TRUE(c.rdp_tls);
  EXPECT_EQ(c.vnc_quality, 90);
  EXPECT_EQ(c.rdp_max_kbps, 0);
}

TEST_F(RemoteConfigTest, QualityAndLimitAreClamped) {
  write_config("remote.toml", "[vnc]\nquality = 500\nmax_kbps = -4\n[rdp]\nquality = 0\n");
  const RemoteConfig c = load_remote_config();
  EXPECT_EQ(c.vnc_quality, 100);
  EXPECT_EQ(c.vnc_max_kbps, 0);
  EXPECT_EQ(c.rdp_quality, 1);
}

TEST_F(RemoteConfigTest, ResolutionAndDepthRoundTripAndAreValidated) {
  RemoteConfig c;
  c.vnc_resolution = "1280x720";
  c.vnc_depth = 8;
  c.rdp_resolution = "800x600";
  c.rdp_depth = 16;
  save_remote_config(c);
  EXPECT_EQ(load_remote_config(), c);
  write_config("remote.toml", "[vnc]\nresolution = \"banana\"\ndepth = 12\n[rdp]\nresolution = \"99999x5\"\ndepth = 8\n");
  const RemoteConfig bad = load_remote_config();
  EXPECT_EQ(bad.vnc_resolution, "");
  EXPECT_EQ(bad.vnc_depth, 0);   // 12 is not a choice
  EXPECT_EQ(bad.rdp_resolution, "");
  EXPECT_EQ(bad.rdp_depth, 0);   // RDP has no 8-bit
}

TEST(RemoteResolution, ParsesAndListsChoices) {
  int w = 1, h = 1;
  EXPECT_TRUE(parse_resolution("1280x720", &w, &h));
  EXPECT_EQ(w, 1280);
  EXPECT_EQ(h, 720);
  EXPECT_FALSE(parse_resolution("", &w, &h));
  EXPECT_FALSE(parse_resolution("1280", &w, &h));
  EXPECT_FALSE(parse_resolution("10x10", &w, &h));
  EXPECT_FALSE(parse_resolution("12a0x720", &w, &h));
  for (const auto& c : remote_resolution_choices()) {
    if (std::string(c.value).empty()) continue;
    EXPECT_TRUE(parse_resolution(c.value, &w, &h)) << c.value;
  }
  EXPECT_EQ(nearest_resolution_index("nonsense"), 0);
  EXPECT_EQ(depth_index(true, 32), 2);
  EXPECT_EQ(depth_index(false, 24), 3);
}

TEST(RemoteBandwidth, ChoicesAndLabels) {
  const auto& c = remote_bandwidth_choices();
  ASSERT_GE(c.size(), 5u);
  EXPECT_EQ(c.front(), 0);
  EXPECT_EQ(remote_bandwidth_label(0), "No limit");
  EXPECT_EQ(remote_bandwidth_label(128), "128 KB/s (1 Mbit/s)");
  EXPECT_EQ(remote_bandwidth_label(32), "32 KB/s (256 kbit/s)");
  EXPECT_EQ(nearest_bandwidth_index(0), 0);
  EXPECT_EQ(c[static_cast<size_t>(nearest_bandwidth_index(300))], 256);  // a hand-edited value shows as the closest choice
}

TEST(RemotePassword, HasTheLengthAndOnlyUnambiguousCharacters) {
  std::set<std::string> seen;
  for (int i = 0; i < 50; ++i) {
    const std::string p = generate_remote_password(16);
    ASSERT_EQ(p.size(), 16u);
    for (char ch : p) EXPECT_EQ(std::string("0O1lI").find(ch), std::string::npos) << p;
    EXPECT_TRUE(std::all_of(p.begin(), p.end(), [](unsigned char ch) { return std::isalnum(ch); })) << p;
    seen.insert(p);
  }
  EXPECT_EQ(seen.size(), 50u);  // 57^16 possibilities: a repeat means the random source is broken
}

TEST_F(RemoteConfigTest, FrameRateAndKeptPasswordRoundTrip) {
  RemoteConfig c;
  c.vnc_enabled = true;
  c.vnc_password = "abcd1234";
  c.vnc_password_keep = true;
  c.vnc_fps = 24;
  c.rdp_fps = 60;
  save_remote_config(c);
  const RemoteConfig r = load_remote_config();
  EXPECT_EQ(r.vnc_fps, 24);
  EXPECT_EQ(r.rdp_fps, 60);
  EXPECT_TRUE(r.vnc_password_keep);
  EXPECT_FALSE(r.rdp_password_keep);
  EXPECT_EQ(RemoteConfig{}.vnc_fps, kFpsDefault);
}

TEST(RemoteFps, ChoicesAndTheBandwidthFloor) {
  const auto& c = remote_fps_choices();
  EXPECT_NE(std::find(c.begin(), c.end(), 24), c.end());
  EXPECT_EQ(c[static_cast<size_t>(nearest_fps_index(25))], 24);
  EXPECT_EQ(c[static_cast<size_t>(nearest_fps_index(1000))], 60);
  // More frames, more pixels or more quality all need more bandwidth.
  EXPECT_LT(remote_min_kbps(40, 15, 1920, 1080), remote_min_kbps(40, 30, 1920, 1080));
  EXPECT_LT(remote_min_kbps(40, 24, 1280, 720), remote_min_kbps(40, 24, 1920, 1080));
  EXPECT_LT(remote_min_kbps(60, 24, 1920, 1080), remote_min_kbps(100, 24, 1920, 1080));
  EXPECT_GE(remote_min_kbps(1, 1, 1, 1), 1);
}
