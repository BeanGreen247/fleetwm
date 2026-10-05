#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace fleetwm {

// Index of the Settings tab named `page` ("power" -> the "Power" tab), or -1.
// Matching ignores case and treats a page as a match when the tab name starts with
// it, so "date" finds "Date & Time" and "default" finds "Default Apps" (first match wins).
inline int find_settings_page(const std::vector<std::string>& tab_names, const std::string& page) {
  auto lower = [](std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
  };
  const std::string want = lower(page);
  if (want.empty()) return -1;
  for (size_t i = 0; i < tab_names.size(); ++i)
    if (lower(tab_names[i]) == want) return static_cast<int>(i);
  for (size_t i = 0; i < tab_names.size(); ++i)
    if (lower(tab_names[i]).rfind(want, 0) == 0) return static_cast<int>(i);
  return -1;
}

}  // namespace fleetwm
