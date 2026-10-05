#include "window_list.hpp"

#include <algorithm>

namespace fleetwm {

namespace {

std::string clean(std::string s) {
  std::replace_if(s.begin(), s.end(), [](char c) { return c == '\t' || c == '\n' || c == '\r'; }, ' ');
  return s;
}

}  // namespace

std::string format_window_list(const std::vector<WindowEntry>& windows) {
  std::string line = "WINDOWS";
  for (const WindowEntry& w : windows) {
    line += '\t' + std::to_string(w.id);
    std::string flags;
    if (w.focused) flags += 'F';
    if (w.minimized) flags += 'M';
    line += '\t' + (flags.empty() ? std::string("-") : flags);
    line += '\t' + clean(w.app_id);
    line += '\t' + clean(w.title);
  }
  return line;
}

bool parse_window_list(const std::string& line, std::vector<WindowEntry>* out) {
  out->clear();
  std::vector<std::string> fields;
  size_t start = 0;
  for (;;) {
    const size_t tab = line.find('\t', start);
    fields.push_back(line.substr(start, tab == std::string::npos ? tab : tab - start));
    if (tab == std::string::npos) break;
    start = tab + 1;
  }
  if (fields.empty() || fields[0] != "WINDOWS" || (fields.size() - 1) % 4 != 0) return false;

  for (size_t i = 1; i < fields.size(); i += 4) {
    WindowEntry w;
    const std::string& id = fields[i];
    if (id.empty() || id.find_first_not_of("0123456789") != std::string::npos || id.size() > 9) {
      out->clear();
      return false;
    }
    w.id = static_cast<uint32_t>(std::stoul(id));
    const std::string& flags = fields[i + 1];
    if (flags.find_first_not_of("-FM") != std::string::npos) {
      out->clear();
      return false;
    }
    w.focused = flags.find('F') != std::string::npos;
    w.minimized = flags.find('M') != std::string::npos;
    w.app_id = fields[i + 2];
    w.title = fields[i + 3];
    out->push_back(std::move(w));
  }
  return true;
}

}  // namespace fleetwm
