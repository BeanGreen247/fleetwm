#pragma once

// Persisted per-monitor settings (resolution/refresh and position in the
// desktop layout), keyed by output name ("DP-1", "HDMI-A-1", "Virtual-1").
// Stored in $XDG_CONFIG_HOME/fleetwm/outputs.toml:
//
//   [outputs."DP-1"]
//   width = 2560
//   height = 1440
//   refresh_mhz = 144000
//   x = 0
//   y = 0
//
// The compositor applies an entry when the output appears and whenever it is
// changed (IPC OUTPUT_SET, or fleetwm-settings' Display tab). Missing fields
// mean "use the preferred mode" / "place automatically".

#include <map>
#include <string>

namespace fleetwm {

struct OutputSetting {
  int width = 0, height = 0;  // 0 = preferred mode
  int refresh_mhz = 0;        // 0 = any refresh rate for that size (highest)
  bool has_pos = false;
  int x = 0, y = 0;
};

using OutputSettings = std::map<std::string, OutputSetting>;

struct DisplaySettings {
  std::string primary_output;
  bool taskbar_all_displays = true;
};

std::string output_config_path();
OutputSettings load_output_settings();
DisplaySettings load_display_settings();
// Throws std::runtime_error on I/O failure.
void save_output_settings(const OutputSettings& settings);
void save_display_settings(const DisplaySettings& settings);

}  // namespace fleetwm
