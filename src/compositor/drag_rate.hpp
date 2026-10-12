#pragma once

#include <algorithm>
#include <cstdlib>

// The frame rate cap while a window is being moved or resized (24 FPS). FLEETWM_DRAG_FPS (5..240) overrides it so cap values can be compared
// on one binary; it is read once.
inline int drag_fps() {
  static const int fps = [] {
    const char* e = std::getenv("FLEETWM_DRAG_FPS");
    const int v = e ? std::atoi(e) : 0;
    return v > 0 ? std::clamp(v, 5, 240) : 24;
  }();
  return fps;
}
