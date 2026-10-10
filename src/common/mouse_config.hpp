#pragma once

#include <string>

// Mouse settings (Settings -> Mouse), saved per user in mouse.toml, the way Windows has them:
//   * pointer speed: 11 notches, 6 is the middle and the default;
//   * "Enhance pointer precision": the pointer accelerates with how fast you move the mouse; off, the pointer
//     follows the mouse one to one at any speed;
//   * primary button: left (default) or right, for left-handed use;
//   * natural scrolling: the page follows the finger or wheel (content moves with the wheel instead of against it).

namespace fleetwm {

inline constexpr int kMouseSpeedMin = 1, kMouseSpeedMax = 11, kMouseSpeedDefault = 6;
inline constexpr int kScrollLinesMin = 1, kScrollLinesMax = 20, kScrollLinesDefault = 3;

struct MouseConfig {
  int speed = kMouseSpeedDefault;  // 1..11
  bool enhanced_precision = true;  // acceleration on
  bool swap_buttons = false;       // right button is the primary one
  bool natural_scroll = false;
  int scroll_lines = kScrollLinesDefault;
  bool operator==(const MouseConfig&) const = default;
};

std::string mouse_user_config_path();

// Loads mouse.toml; a missing or broken file gives the defaults. Values are clamped to the notch range.
MouseConfig load_mouse_config();
// Throws std::runtime_error on I/O failure.
void save_mouse_config(const MouseConfig& config);

// libinput's pointer acceleration setting, -1 (slowest) .. +1 (fastest), for a Windows notch: notch 6 is 0.
double libinput_speed_for_notch(int notch);
// True when the device should use the adaptive (accelerating) profile, false for the flat one.
inline bool wants_adaptive_profile(const MouseConfig& config) { return config.enhanced_precision; }

}  // namespace fleetwm
