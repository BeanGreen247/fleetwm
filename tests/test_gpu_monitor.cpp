// GPU discovery and sampling against a fake /sys/class/drm tree (the bar and the Task Manager share GpuMonitor).
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "gpu_monitor.hpp"
#include "test_util.hpp"

using namespace fleetwm;
namespace fs = std::filesystem;

namespace {
class GpuMonitorTest : public testutil::ScopedConfigHome {
 protected:
  fs::path drm() const { return dir_ / "drm"; }
  void put(const std::string& rel, const std::string& text) {
    const fs::path p = drm() / rel;
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text << "\n";
  }
};
}  // namespace

TEST_F(GpuMonitorTest, NoCardsMeansNoGpuAndNaText) {
  fs::create_directories(drm());
  GpuMonitor g;
  g.discover(drm().string(), false);
  EXPECT_FALSE(g.any());
  EXPECT_EQ(g.text(), "GPU N/A");
  EXPECT_EQ(g.busiest_percent(), -1);
  EXPECT_FALSE(g.sample());
}

TEST_F(GpuMonitorTest, AmdReadsItsBusyFile) {
  put("card0/device/vendor", "0x1002");
  put("card0/device/gpu_busy_percent", "37");
  GpuMonitor g;
  g.discover(drm().string(), false);
  ASSERT_EQ(g.devices().size(), 1u);
  EXPECT_EQ(g.devices()[0].vendor, "AMD");
  EXPECT_EQ(g.devices()[0].kind, GpuDevice::Kind::AmdBusy);
  EXPECT_TRUE(g.sample());
  EXPECT_EQ(g.text(), "GPU 37%");
  EXPECT_FALSE(g.sample());  // unchanged
  put("card0/device/gpu_busy_percent", "250");  // out of range is clamped
  EXPECT_TRUE(g.sample());
  EXPECT_EQ(g.devices()[0].percent, 100);
}

TEST_F(GpuMonitorTest, IntelUsesIdleResidencyOverWallClock) {
  put("card0/device/vendor", "0x8086");
  put("card0/gt/gt0/rc6_residency_ms", "1000");
  put("card0/gt/gt0/rps_act_freq_mhz", "350");
  put("card0/gt/gt0/rps_RP0_freq_mhz", "650");
  GpuMonitor g;
  g.discover(drm().string(), false);
  ASSERT_EQ(g.devices().size(), 1u);
  EXPECT_EQ(g.devices()[0].kind, GpuDevice::Kind::IntelIdle);
  EXPECT_FALSE(g.devices()[0].freq_path.empty());
  EXPECT_FALSE(g.devices()[0].freq_max_path.empty());
  EXPECT_FALSE(g.sample());  // the first reading only sets the baseline
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  EXPECT_TRUE(g.sample());  // the sleep-state counter did not move: the GPU never slept, so it was busy the whole time
  EXPECT_EQ(g.devices()[0].percent, 100);
  put("card0/gt/gt0/rc6_residency_ms", "9999999");  // slept at least as long as the wall time: clamped to fully idle
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  g.sample();
  EXPECT_EQ(g.devices()[0].percent, 0);
}

TEST_F(GpuMonitorTest, TwoCardsGiveNumberedText) {
  put("card0/device/vendor", "0x1002");
  put("card0/device/gpu_busy_percent", "10");
  put("card1/device/vendor", "0x1002");
  put("card1/device/gpu_busy_percent", "80");
  GpuMonitor g;
  g.discover(drm().string(), false);
  g.sample();
  EXPECT_EQ(g.text(), "GPU1 10%  GPU2 80%");
  EXPECT_EQ(g.busiest_percent(), 80);
}

TEST_F(GpuMonitorTest, UnknownVendorStillListedByItsId) {
  put("card0/device/vendor", "0xabcd");
  put("card0/device/gpu_busy_percent", "5");
  GpuMonitor g;
  g.discover(drm().string(), false);
  ASSERT_EQ(g.devices().size(), 1u);
  EXPECT_EQ(g.devices()[0].vendor, "0xabcd");
}
