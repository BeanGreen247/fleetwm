#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Wire format for the compositor's window list, sent to subscribed IPC
// clients (the taskbar in fleetwm-bar). One line per snapshot:
//   WINDOWS\t<id>\t<flags>\t<app_id>\t<title>[\t<id>\t...]
// flags is "-" or a mix of F (focused) and M (minimized). Tabs and newlines
// inside app ids and titles are replaced with spaces so the line stays parseable.

namespace fleetwm {

struct WindowEntry {
  uint32_t id = 0;
  bool focused = false;
  bool minimized = false;
  std::string app_id;
  std::string title;

  bool operator==(const WindowEntry&) const = default;
};

// Full IPC line (without the trailing newline).
std::string format_window_list(const std::vector<WindowEntry>& windows);

// Parses a line produced by format_window_list(). Returns false (and leaves
// *out empty) if it does not start with "WINDOWS" or is malformed.
bool parse_window_list(const std::string& line, std::vector<WindowEntry>* out);

}  // namespace fleetwm
