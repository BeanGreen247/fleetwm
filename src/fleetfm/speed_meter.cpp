#include "speed_meter.hpp"

#include <cmath>

namespace fleetwm::fm {

void SpeedMeter::update(double now, uint64_t bytes) {
  if (last_t_ < 0) {
    last_t_ = now;
    last_bytes_ = bytes;
    return;
  }
  const double dt = now - last_t_;
  if (dt < 0.05) return;
  const double inst = bytes >= last_bytes_ ? static_cast<double>(bytes - last_bytes_) / dt : 0.0;
  const double alpha = 1.0 - std::exp(-dt / window_);
  rate_ = rate_ == 0 ? inst : rate_ + alpha * (inst - rate_);
  last_t_ = now;
  last_bytes_ = bytes;
}

}  // namespace fleetwm::fm
