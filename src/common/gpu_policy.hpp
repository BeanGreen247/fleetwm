#pragma once

#include <string>
#include <vector>

#include "game_config.hpp"

// Which graphics card draws the desktop. On a computer with two (integrated + dedicated) the dedicated one is left to the programs that ask for it, the way
// GNOME, KDE and switcheroo-control do it: the integrated GPU is the "default" one (the boot VGA device) and renders the desktop; a game that wants the other
// card is started with PRIME render offload and hands its picture over as a dmabuf. wlroots takes the FIRST card of WLR_DRM_DEVICES as the primary one
// (the renderer), and every listed card keeps its screens working.

namespace fleetwm {

struct DrmCard {
  std::string path;     // "/dev/dri/card1"
  std::string driver;   // "amdgpu", "i915", "xe", "nvidia", "nouveau", ...
  bool boot_vga = false;  // the firmware's primary display device: the integrated GPU on hybrid machines
  bool has_display = false;  // a screen is connected to one of its outputs
};

// True for a card that is the dedicated one in a hybrid pair: NVIDIA's drivers, or a card that is not the boot device.
bool is_discrete(const DrmCard& card, bool has_other_boot_card);

// The WLR_DRM_DEVICES value for `policy` ("" = leave wlroots to choose: one card, policy Default, or nothing to order). The wanted card goes first, the rest keep their order.
// With Integrated, a dedicated card that has no screen connected is left out altogether: wlroots would open it, start an OpenGL renderer on it and keep it awake
// (17 W and about 70 MB more in the compositor on a gaming laptop) for nothing. A screen plugged into it later (a laptop's HDMI port is often wired to the dedicated
// card) is picked up at the next login; plug it in before logging in, or choose "discrete".
std::string wlr_drm_devices_for(DesktopGpu policy, const std::vector<DrmCard>& cards);

// The cards in /sys/class/drm (card0, card1, ...), as found right now.
std::vector<DrmCard> scan_drm_cards(const std::string& sys_class_drm = "/sys/class/drm");

}  // namespace fleetwm
