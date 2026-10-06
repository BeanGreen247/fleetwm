#pragma once

#include <wayland-server-core.h>

extern "C" {
#include <wlr/types/wlr_scene.h>
}

#include <array>
#include <ctime>
#include <vector>

namespace fleetwm {

class Server;
class Output;

// The Alt+Shift+I performance overlay of one output: ONE scene buffer (a small cairo-drawn picture)
// instead of hundreds of one-pixel-cell scene rectangles. It is redrawn at most four times a second,
// and inside the frame that is being committed anyway, so it adds no frames of its own while the
// desktop is busy and at most one while it goes idle (to show 0 FPS). Shows what answers "does this
// hold 60 fps": frames per second, the time between frames, what the compositor itself spent per
// frame (build + commit), late frames, CPU, memory, and a graph of recent frame intervals.
class DebugOverlay {
 public:
  DebugOverlay(Server* server, Output* output);
  ~DebugOverlay();
  DebugOverlay(const DebugOverlay&) = delete;
  DebugOverlay& operator=(const DebugOverlay&) = delete;

  // Called from the frame handler right before the commit: records the frame interval and, when a
  // refresh is due, repaints the picture so it goes out with this very commit.
  void frame_begin(const timespec& now);
  // Called right after the commit with the time the compositor spent on it.
  void frame_end(double cost_ms);

 private:
  static constexpr int kSamples = 96;
  static constexpr int kRefreshMs = 250;

  struct Window {  // statistics since the last repaint
    int frames = 0, late = 0;
    double delta_sum = 0, delta_max = 0, cost_sum = 0, cost_max = 0, first_ms = 0, last_ms = 0;
  };

  void repaint(double now_ms);
  void arm_idle_timer();
  static int idle_fired(void* data);
  double budget_ms() const;

  Server* server_;
  Output* output_;
  wlr_scene_buffer* node_ = nullptr;
  wl_event_source* idle_timer_ = nullptr;

  std::array<float, kSamples> deltas_{};  // recent frame intervals, oldest first once full
  int next_ = 0;
  Window win_;
  double last_frame_ms_ = -1, last_repaint_ms_ = -1;
  bool idle_shown_ = false;  // the picture says "0 FPS"; the next frame is probably our own repaint
  // CPU time used at the previous repaint, for the share over a repaint interval.
  double prev_cpu_us_ = 0, prev_cpu_ms_ = 0;
  int cpu_pct_ = 0, mem_mb_ = 0, mhz_ = -1;
  int slow_age_ = 0;  // repaints until memory and clock speed are read again
  std::vector<unsigned char> background_;  // the panel and the budget line, drawn once
};

}  // namespace fleetwm
