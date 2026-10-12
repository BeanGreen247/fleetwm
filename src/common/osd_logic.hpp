#pragma once

// The pure parts of fleetwm-osd, the volume and brightness pop-up: the commands the compositor's media keys send, the percent steps,
// the readings of wpctl and of /sys/class/backlight, and how many bar segments are lit. No Wayland, no D-Bus here, so the tests cover it.

#include <string>

namespace fleetwm::osd {

enum class Kind { Volume, Brightness };
enum class Action { Up, Down, Mute, Show };  // Mute only means something for the volume

struct Command {
  Kind kind = Kind::Volume;
  Action action = Action::Show;
};

// "volume up", "volume down", "volume mute", "volume show", "brightness up", "brightness down", "brightness show" (one line, as sent
// over the socket and given on the command line). False for anything else, including "brightness mute".
bool parse_command(const std::string& line, Command* out);

// One key press moves the level to the next multiple of `step` percent, 0..100.
int step_percent(int percent, bool up, int step = 5);

// `wpctl get-volume` prints "Volume: 0.45" or "Volume: 0.45 [MUTED]".
bool parse_wpctl_volume(const std::string& text, int* percent, bool* muted);

struct Backlight {
  std::string name;  // directory name under /sys/class/backlight
  long current = 0, max = 0;
};
// The backlight with the largest max_brightness under `root` (/sys/class/backlight, or a fake tree in tests).
bool read_backlight(const std::string& root, Backlight* out);
int backlight_percent(long current, long max);
// The raw value for a percent; never 0, so the screen cannot be turned fully black by the keys.
long backlight_value(int percent, long max);

// Lit segments of a bar of `segments` for 0..100 percent; any level above 0 lights at least one.
int lit_segments(int percent, int segments = 16);

}  // namespace fleetwm::osd
