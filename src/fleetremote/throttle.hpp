#pragma once

// What a viewer is allowed to cost: the quality and bandwidth limit from Settings, and what the link turns out to
// carry. Quality 1-100 sets how many low bits of each colour channel are dropped (a flat colour compresses to almost
// nothing, noise and gradients much better) and the shortest time between two updates. The bandwidth limit (KB/s,
// 0 = none) paces the sends; when a viewer takes more than the budget the colour depth drops one more bit at a time
// and returns when there is room. The link's own speed is learned from how long the sends take, so a viewer on a
// slow connection is not flooded even with no limit set.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>

#include "hub.hpp"
#include "net_util.hpp"

namespace fleetremote {

inline int quality_quant_bits(int q) { return q >= 85 ? 0 : q >= 70 ? 1 : q >= 50 ? 2 : q >= 30 ? 3 : 4; }
inline int quality_interval_ms(int q) { return q >= 85 ? 25 : q >= 70 ? 50 : q >= 50 ? 100 : q >= 30 ? 200 : 400; }

// Drops the low `bits` bits of each channel of 0x00RRGGBB pixels and puts the channel's midpoint back so the picture does
// not darken. 0 bits leaves the pixels alone.
inline void quantize_pixels(uint32_t* px, size_t n, int bits) {
  if (bits <= 0) return;
  const uint32_t mask = ((0xFFu << bits) & 0xFFu) * 0x010101u;
  const uint32_t mid = ((1u << bits) >> 1) * 0x010101u;
  for (size_t i = 0; i < n; ++i) px[i] = (px[i] & mask) | mid;
}

// Limits the colours to what a `depth`-bit pixel can hold (8: 3-3-2 bits, 16: 5-6-5, anything else: untouched), keeping each
// channel's midpoint like quantize_pixels.
inline void quantize_to_depth(uint32_t* px, size_t n, int depth) {
  uint32_t mask, mid;
  if (depth == 8) {
    mask = 0xE0E0C0u;
    mid = 0x101020u;
  } else if (depth == 16) {
    mask = 0xF8FCF8u;
    mid = 0x040204u;
  } else {
    return;
  }
  for (size_t i = 0; i < n; ++i) px[i] = (px[i] & mask) | mid;
}

class Throttle {
 public:
  explicit Throttle(const Tuning& t) : tuning_(t) { window_start_ = Clock::now(); }

  int quality() const { return tuning_.quality.load(); }
  // Bits to drop now: from the quality setting plus whatever the congestion control has added.
  int quant_bits() const { return std::min(5, quality_quant_bits(quality()) + extra_); }
  // Bytes per second the viewer may use, 0 when nothing limits it.
  double budget() const {
    double b = 0;
    const int kb = tuning_.max_kbps.load();
    if (kb > 0) b = kb * 1024.0;
    if (link_ > 0 && (b == 0 || link_ * 0.8 < b)) b = link_ * 0.8;
    return b;
  }
  // The earliest time the next update may go out.
  Clock::time_point next_allowed() const {
    const auto interval = std::chrono::milliseconds(quality_interval_ms(quality()));
    return std::max(last_start_ + interval, paced_until_);
  }
  // Call after an update of `bytes` that took `took` to hand to the network.
  void sent(size_t bytes, Clock::duration took, Clock::time_point started) {
    last_start_ = started;
    total_ += bytes;
    ++updates_;
    const double secs = std::chrono::duration<double>(took).count();
    if (bytes >= 8192 && secs > 0.002) {  // a send that blocked shows what the link carries
      const double rate = static_cast<double>(bytes) / secs;
      link_ = link_ > 0 ? 0.7 * link_ + 0.3 * rate : rate;
    }
    const double b = budget();
    paced_until_ = b > 0 ? started + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(static_cast<double>(bytes) / b)) : started;
    window_bytes_ += bytes;
    const auto now = Clock::now();
    const double span = std::chrono::duration<double>(now - window_start_).count();
    if (span >= 1.0) {  // once a second: too much -> a bit less colour, plenty of room -> a bit more back
      if (b > 0) {
        const double used = window_bytes_ / span / b;
        if (used > 0.95 && extra_ < 3) ++extra_;
        else if (used < 0.4 && extra_ > 0) --extra_;
      } else if (extra_ > 0) {
        --extra_;
      }
      window_bytes_ = 0;
      window_start_ = now;
    }
  }
  uint64_t total_bytes() const { return total_; }
  uint64_t updates() const { return updates_; }

 private:
  const Tuning& tuning_;
  Clock::time_point last_start_{}, paced_until_{}, window_start_{};
  double link_ = 0;  // bytes/s the link was seen to carry, 0 = not known
  double window_bytes_ = 0;
  int extra_ = 0;
  uint64_t total_ = 0, updates_ = 0;
};

}  // namespace fleetremote
