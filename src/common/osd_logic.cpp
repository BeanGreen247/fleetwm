#include "osd_logic.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fleetwm::osd {

bool parse_command(const std::string& line, Command* out) {
  const size_t sp = line.find(' ');
  if (sp == std::string::npos) return false;
  const std::string kind = line.substr(0, sp);
  std::string action = line.substr(sp + 1);
  while (!action.empty() && (action.back() == '\n' || action.back() == '\r' || action.back() == ' ')) action.pop_back();
  Command c;
  if (kind == "volume") c.kind = Kind::Volume;
  else if (kind == "brightness") c.kind = Kind::Brightness;
  else return false;
  if (action == "up") c.action = Action::Up;
  else if (action == "down") c.action = Action::Down;
  else if (action == "show") c.action = Action::Show;
  else if (action == "mute" && c.kind == Kind::Volume) c.action = Action::Mute;
  else return false;
  *out = c;
  return true;
}

int step_percent(int percent, bool up, int step) {
  percent = std::clamp(percent, 0, 100);
  if (step < 1) step = 1;
  const int next = up ? (percent / step + 1) * step : ((percent + step - 1) / step - 1) * step;
  return std::clamp(next, 0, 100);
}

bool parse_wpctl_volume(const std::string& text, int* percent, bool* muted) {
  const size_t at = text.find("Volume:");
  if (at == std::string::npos) return false;
  char* end = nullptr;
  const double v = std::strtod(text.c_str() + at + 7, &end);
  if (end == text.c_str() + at + 7 || v < 0) return false;
  *percent = static_cast<int>(v * 100.0 + 0.5);
  *muted = text.find("[MUTED]") != std::string::npos;
  return true;
}

bool read_backlight(const std::string& root, Backlight* out) {
  namespace fs = std::filesystem;
  std::error_code ec;
  bool found = false;
  for (const fs::directory_entry& e : fs::directory_iterator(root, ec)) {
    long cur = 0, max = 0;
    std::ifstream(e.path() / "brightness") >> cur;
    std::ifstream(e.path() / "max_brightness") >> max;
    if (max <= 0) continue;
    if (!found || max > out->max) {
      out->name = e.path().filename().string();
      out->current = cur;
      out->max = max;
      found = true;
    }
  }
  return found;
}

int backlight_percent(long current, long max) {
  if (max <= 0) return 0;
  return static_cast<int>(std::clamp(100.0 * static_cast<double>(current) / static_cast<double>(max) + 0.5, 0.0, 100.0));
}

long backlight_value(int percent, long max) {
  if (max <= 0) return 0;
  const long v = static_cast<long>(static_cast<double>(std::clamp(percent, 0, 100)) * static_cast<double>(max) / 100.0 + 0.5);
  return std::clamp(v, 1L, max);
}

int lit_segments(int percent, int segments) {
  percent = std::clamp(percent, 0, 100);
  if (percent == 0) return 0;
  return std::clamp(static_cast<int>(static_cast<double>(percent) * segments / 100.0 + 0.5), 1, segments);
}

}  // namespace fleetwm::osd
