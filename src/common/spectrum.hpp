#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <vector>

// Turns a stream of mono audio samples into a few smoothed band levels (0..1) for the bar's
// media spectrum. feed() is called from the audio thread, levels() may be read from any other.

namespace fleetwm {

class SpectrumAnalyzer {
 public:
  static constexpr int kMaxBands = 48;
  static constexpr size_t kWindow = 1024;  // samples per transform (a power of two)

  explicit SpectrumAnalyzer(int bands = 32, double sample_rate = 48000.0);

  // Adds samples; runs a transform for every half window collected.
  void feed(const float* mono, size_t count);
  // Lets the levels fall as if silence had arrived (no audio callbacks while nothing plays).
  void decay();
  // Copies the current band levels, each 0..1, into `out` (bands() values).
  void levels(float* out) const;
  int bands() const { return bands_; }

 private:
  void transform();

  int bands_;
  double sample_rate_;
  std::vector<float> ring_;
  size_t fill_ = 0, since_transform_ = 0;
  std::vector<float> window_, re_, im_;
  std::vector<int> edge_;  // FFT bin edges per band, bands_ + 1 values
  std::array<std::atomic<float>, kMaxBands> level_{};
};

}  // namespace fleetwm
