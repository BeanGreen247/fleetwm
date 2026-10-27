#include "gpu_policy.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace fleetwm {
namespace {

const DrmCard kAmdIgpu{"/dev/dri/card1", "amdgpu", true};
const DrmCard kNvidia{"/dev/dri/card2", "nvidia", false, true};         // a screen is plugged into it
const DrmCard kNvidiaIdle{"/dev/dri/card2", "nvidia", false, false};     // nothing connected to it
const DrmCard kIntel{"/dev/dri/card0", "i915", true};
const DrmCard kAmdDgpu{"/dev/dri/card1", "amdgpu", false};

TEST(GpuPolicy, IntegratedFirstOnTheGamingPc) {
  EXPECT_EQ(wlr_drm_devices_for(DesktopGpu::Integrated, {kAmdIgpu, kNvidia}), "/dev/dri/card1:/dev/dri/card2");
  EXPECT_EQ(wlr_drm_devices_for(DesktopGpu::Integrated, {kNvidia, kAmdIgpu}), "/dev/dri/card1:/dev/dri/card2");
}

TEST(GpuPolicy, ADedicatedCardWithoutAScreenIsLeftClosedSoItCanSleep) {
  EXPECT_EQ(wlr_drm_devices_for(DesktopGpu::Integrated, {kAmdIgpu, kNvidiaIdle}), "/dev/dri/card1");
  EXPECT_EQ(wlr_drm_devices_for(DesktopGpu::Discrete, {kAmdIgpu, kNvidiaIdle}), "/dev/dri/card2:/dev/dri/card1") << "asked for it: both stay open";
}

TEST(GpuPolicy, ScanReadsConnectedOutputs) {
  namespace fs = std::filesystem;
  const fs::path root = fs::temp_directory_path() / "fleetwm-gpu-policy-scan";
  fs::remove_all(root);
  auto put = [&](const std::string& rel, const std::string& text) {
    fs::create_directories((root / rel).parent_path());
    std::ofstream(root / rel) << text << "\n";
  };
  put("card1/device/boot_vga", "1");
  put("card1-eDP-1/status", "connected");
  put("card2/device/boot_vga", "0");
  put("card2-HDMI-A-1/status", "disconnected");
  const std::vector<DrmCard> cards = scan_drm_cards(root.string());
  ASSERT_EQ(cards.size(), 2u);
  EXPECT_TRUE(cards[0].boot_vga);
  EXPECT_TRUE(cards[0].has_display);
  EXPECT_FALSE(cards[1].has_display);
  fs::remove_all(root);
}

TEST(GpuPolicy, DiscreteFirstWhenAsked) {
  EXPECT_EQ(wlr_drm_devices_for(DesktopGpu::Discrete, {kAmdIgpu, kNvidia}), "/dev/dri/card2:/dev/dri/card1");
}

TEST(GpuPolicy, DefaultAndSingleCardsAreLeftToWlroots) {
  EXPECT_EQ(wlr_drm_devices_for(DesktopGpu::Default, {kAmdIgpu, kNvidia}), "");
  EXPECT_EQ(wlr_drm_devices_for(DesktopGpu::Integrated, {kIntel}), "");
  EXPECT_EQ(wlr_drm_devices_for(DesktopGpu::Integrated, {}), "");
}

TEST(GpuPolicy, IntelPlusAmdDedicatedCard) {
  EXPECT_EQ(wlr_drm_devices_for(DesktopGpu::Integrated, {kAmdDgpu, kIntel}), "/dev/dri/card0") << "no screen on the AMD card: left closed";
  DrmCard with_screen = kAmdDgpu;
  with_screen.has_display = true;
  EXPECT_EQ(wlr_drm_devices_for(DesktopGpu::Integrated, {with_screen, kIntel}), "/dev/dri/card0:/dev/dri/card1");
}

TEST(GpuPolicy, TwoCardsOfOneKindHaveNothingToPrefer) {
  EXPECT_EQ(wlr_drm_devices_for(DesktopGpu::Integrated, {kIntel, DrmCard{"/dev/dri/card1", "i915", true}}), "");
}

}  // namespace
}  // namespace fleetwm
