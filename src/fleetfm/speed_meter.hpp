#pragma once

#include <cstdint>

// Transfer speed and time remaining that do not jump around: an exponential average of the rate between samples, so a
// burst of cached writes at the start does not promise "2 seconds" for a 20-minute copy. Time is passed in, which keeps
// it testable.

namespace fleetwm::fm {

class SpeedMeter {
 public:
  explicit SpeedMeter(double window_seconds = 4.0) : window_(window_seconds) {}
  // Call as often as you like with the running byte count; samples closer than 50 ms are folded into the next.
  void update(double now_seconds, uint64_t bytes_done);
  double bytes_per_second() const { return rate_; }
  // Seconds left for `remaining` bytes, or -1 until a rate is known.
  double eta_seconds(uint64_t remaining) const { return rate_ > 1.0 ? static_cast<double>(remaining) / rate_ : -1.0; }
  void reset() { *this = SpeedMeter(window_); }

 private:
  double window_;
  double last_t_ = -1, rate_ = 0;
  uint64_t last_bytes_ = 0;
};

}  // namespace fleetwm::fm
