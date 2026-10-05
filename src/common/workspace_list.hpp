#pragma once

#include <algorithm>
#include <vector>

namespace fleetwm {

// Which workspace buttons the bar shows, in numerical order: the first `minimum`
// workspaces always, plus every workspace that has a window, plus the one you are on
// (so moving to a new workspace creates its button, and it goes away again once you
// leave it empty). `occupied` holds one workspace index per window, duplicates allowed.
// Indices are 0-based and limited to 0..max_count-1.
inline std::vector<int> visible_workspaces(const std::vector<int>& occupied, int active, int minimum,
                                           int max_count = 10) {
  std::vector<int> out;
  auto add = [&](int i) {
    if (i >= 0 && i < max_count) out.push_back(i);
  };
  for (int i = 0; i < std::clamp(minimum, 0, max_count); ++i) add(i);
  for (int w : occupied) add(w);
  add(active);
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

}  // namespace fleetwm
