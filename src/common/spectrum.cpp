#include "spectrum.hpp"

#include <algorithm>
#include <cmath>

namespace fleetwm {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kLowHz = 50.0, kHighHz = 14000.0;
constexpr float kFloorDb = -70.0f;  // a band this quiet shows nothing
constexpr float kRelease = 0.82f;   // fraction of the level kept per transform when the sound drops

// In-place radix-2 FFT; n is a power of two.
void fft(float* re, float* im, size_t n) {
  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      std::swap(re[i], re[j]);
      std::swap(im[i], im[j]);
    }
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    const double ang = -2.0 * kPi / static_cast<double>(len);
    const float wr = static_cast<float>(std::cos(ang)), wi = static_cast<float>(std::sin(ang));
    for (size_t i = 0; i < n; i += len) {
      float cr = 1.0f, ci = 0.0f;
      for (size_t k = 0; k < len / 2; ++k) {
        const size_t a = i + k, b = i + k + len / 2;
        const float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr;
        im[b] = im[a] - ti;
        re[a] += tr;
        im[a] += ti;
        const float nr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = nr;
      }
    }
  }
}

}  // namespace

SpectrumAnalyzer::SpectrumAnalyzer(int bands, double sample_rate)
    : bands_(std::clamp(bands, 1, kMaxBands)),
      sample_rate_(sample_rate),
      ring_(kWindow, 0.0f),
      window_(kWindow),
      re_(kWindow),
      im_(kWindow),
      edge_(static_cast<size_t>(bands_) + 1) {
  for (size_t i = 0; i < kWindow; ++i) window_[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * i / (kWindow - 1)));
  // Log-spaced bands, each at least one bin wide.
  const double bin_hz = sample_rate_ / static_cast<double>(kWindow);
  int prev = std::max(1, static_cast<int>(kLowHz / bin_hz));
  edge_[0] = prev;
  for (int b = 1; b <= bands_; ++b) {
    const double hz = kLowHz * std::pow(kHighHz / kLowHz, static_cast<double>(b) / bands_);
    prev = std::max(prev + 1, static_cast<int>(hz / bin_hz));
    edge_[static_cast<size_t>(b)] = std::min<int>(prev, static_cast<int>(kWindow / 2));
  }
  for (auto& l : level_) l.store(0.0f, std::memory_order_relaxed);
}

void SpectrumAnalyzer::feed(const float* mono, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    ring_[fill_] = mono[i];
    fill_ = (fill_ + 1) % kWindow;
    if (++since_transform_ >= kWindow / 2) {
      since_transform_ = 0;
      transform();
    }
  }
}

void SpectrumAnalyzer::transform() {
  for (size_t i = 0; i < kWindow; ++i) {  // oldest sample first
    re_[i] = ring_[(fill_ + i) % kWindow] * window_[i];
    im_[i] = 0.0f;
  }
  fft(re_.data(), im_.data(), kWindow);
  for (int b = 0; b < bands_; ++b) {
    const int lo = edge_[static_cast<size_t>(b)], hi = std::max(lo + 1, edge_[static_cast<size_t>(b) + 1]);
    float peak = 0.0f;
    for (int k = lo; k < hi && k < static_cast<int>(kWindow / 2); ++k) {
      const float m = std::sqrt(re_[static_cast<size_t>(k)] * re_[static_cast<size_t>(k)] + im_[static_cast<size_t>(k)] * im_[static_cast<size_t>(k)]);
      peak = std::max(peak, m);
    }
    // A full-scale sine gives a bin magnitude of about kWindow / 4 with the Hann window.
    const float norm = peak / (kWindow / 4.0f);
    const float db = 20.0f * std::log10(std::max(norm, 1e-6f));
    const float target = std::clamp((db - kFloorDb) / -kFloorDb, 0.0f, 1.0f);
    const float old = level_[static_cast<size_t>(b)].load(std::memory_order_relaxed);
    level_[static_cast<size_t>(b)].store(target > old ? target : old * kRelease + target * (1.0f - kRelease), std::memory_order_relaxed);
  }
}

void SpectrumAnalyzer::decay() {
  for (int b = 0; b < bands_; ++b) {
    auto& l = level_[static_cast<size_t>(b)];
    l.store(l.load(std::memory_order_relaxed) * kRelease, std::memory_order_relaxed);
  }
}

void SpectrumAnalyzer::levels(float* out) const {
  for (int b = 0; b < bands_; ++b) out[b] = level_[static_cast<size_t>(b)].load(std::memory_order_relaxed);
}

}  // namespace fleetwm
