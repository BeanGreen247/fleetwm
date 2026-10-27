#pragma once

#include <algorithm>
#include <vector>

namespace fleetwm {

// The recent-windows order after an Alt+Tab cycle ends: the window the cycle settled on first, then every other window in the order it had when the
// cycle began. Stepping through B and C to reach D therefore leaves D, A, B, C (not D, C, B, A): the next Alt+Tab returns to A, the window you came from.
template <class T>
std::vector<T> window_cycle_order(const std::vector<T>& before, const T& chosen) {
  std::vector<T> out{chosen};
  for (const T& w : before)
    if (!(w == chosen)) out.push_back(w);
  return out;
}

}  // namespace fleetwm
