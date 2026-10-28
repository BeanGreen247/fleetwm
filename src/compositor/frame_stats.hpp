#pragma once

// Frame pacing of one output, always on and cheap: the time between two presented frames and how long the compositor's own commit took, for the last
// kFrameStatsWindow frames. `fleetwm-ctl frame-stats` prints the summary: it is how stutter is told from slow rendering (a hitch shows as one long interval
// with a short commit, a slow compositor as long commits), for a game, osu!lazer's exit, or the desktop itself.

#include <algorithm>
#include <cstddef>
#include <vector>

namespace fleetwm {

inline constexpr size_t kFrameStatsWindow = 1200;  // about 8 seconds at 144 Hz, 20 seconds at 60

struct FrameSummary {
  size_t frames = 0;
  double fps = 0;                    // frames per second over the window
  double p50 = 0, p95 = 0, p99 = 0;  // presented-frame intervals, ms
  double max_interval = 0;
  size_t over_1_5 = 0, over_3 = 0;   // intervals longer than 1.5 and 3 refreshes: missed frames and hitches
  double work_avg = 0, work_max = 0; // the compositor's commit cost per frame, ms
};

class FrameStats {
 public:
  // interval_ms: since the previous presented frame (<= 0 for the first one, then ignored).
  void add_interval(double interval_ms) {
    if (interval_ms <= 0) return;
    push(intervals_, interval_ms);
  }
  void add_work(double work_ms) {
    if (work_ms < 0) return;
    push(work_, work_ms);
  }
  void reset() {
    intervals_.clear();
    work_.clear();
    next_i_ = next_w_ = 0;
  }

  // refresh_ms: one refresh of the output (0 when unknown: the over_* counts stay 0).
  FrameSummary summary(double refresh_ms) const {
    FrameSummary s;
    s.frames = intervals_.size();
    if (!intervals_.empty()) {
      std::vector<double> sorted = intervals_;
      std::sort(sorted.begin(), sorted.end());
      auto pct = [&](double p) { return sorted[std::min(sorted.size() - 1, static_cast<size_t>(p * static_cast<double>(sorted.size())))]; };
      s.p50 = pct(0.50);
      s.p95 = pct(0.95);
      s.p99 = pct(0.99);
      s.max_interval = sorted.back();
      double total = 0;
      for (double v : sorted) {
        total += v;
        if (refresh_ms > 0 && v > refresh_ms * 1.5) ++s.over_1_5;
        if (refresh_ms > 0 && v > refresh_ms * 3.0) ++s.over_3;
      }
      s.fps = total > 0 ? 1000.0 * static_cast<double>(sorted.size()) / total : 0;
    }
    if (!work_.empty()) {
      double total = 0;
      for (double v : work_) {
        total += v;
        s.work_max = std::max(s.work_max, v);
      }
      s.work_avg = total / static_cast<double>(work_.size());
    }
    return s;
  }

 private:
  void push(std::vector<double>& ring, double v) {
    size_t& next = &ring == &intervals_ ? next_i_ : next_w_;
    if (ring.size() < kFrameStatsWindow) {
      ring.push_back(v);
    } else {
      ring[next] = v;
      next = (next + 1) % kFrameStatsWindow;
    }
  }
  std::vector<double> intervals_, work_;
  size_t next_i_ = 0, next_w_ = 0;
};

}  // namespace fleetwm
