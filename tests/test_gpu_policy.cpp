#include "gpu_policy.hpp"

#include <gtest/gtest.h>

namespace fleetwm {
namespace {

const DrmCard kAmdIgpu{"/dev/dri/card1", "amdgpu", true};
const DrmCard kNvidia{"/dev/dri/card2", "nvidia", false};
const DrmCard kIntel{"/dev/dri/card0", "i915", true};
const DrmCard kAmdDgpu{"/dev/dri/card1", "amdgpu", false};

TEST(GpuPolicy, IntegratedFirstOnTheGamingPc) {
  EXPECT_EQ(wlr_drm_devices_for(DesktopGpu::Integrated, {kAmdIgpu, kNvidia}), "/dev/dri/card1:/dev/dri/card2");
  EXPECT_EQ(wlr_drm_devices_for(DesktopGpu::Integrated, {kNvidia, kAmdIgpu}), "/dev/dri/card1:/dev/dri/card2");
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
  EXPECT_EQ(wlr_drm_devices_for(DesktopGpu::Integrated, {kAmdDgpu, kIntel}), "/dev/dri/card0:/dev/dri/card1");
}

TEST(GpuPolicy, TwoCardsOfOneKindHaveNothingToPrefer) {
  EXPECT_EQ(wlr_drm_devices_for(DesktopGpu::Integrated, {kIntel, DrmCard{"/dev/dri/card1", "i915", true}}), "");
}

}  // namespace
}  // namespace fleetwm
