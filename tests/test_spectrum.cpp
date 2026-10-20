#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "spectrum.hpp"

using namespace fleetwm;

namespace {
std::vector<float> sine(double hz, size_t n, double rate = 48000.0) {
  std::vector<float> s(n);
  for (size_t i = 0; i < n; ++i) s[i] = static_cast<float>(0.8 * std::sin(2.0 * 3.14159265358979 * hz * i / rate));
  return s;
}
int loudest(const SpectrumAnalyzer& a) {
  float l[SpectrumAnalyzer::kMaxBands];
  a.levels(l);
  int best = 0;
  for (int i = 1; i < a.bands(); ++i)
    if (l[i] > l[best]) best = i;
  return best;
}
}  // namespace

TEST(SpectrumAnalyzer, SilenceIsFlat) {
  SpectrumAnalyzer a(24);
  const std::vector<float> zero(4096, 0.0f);
  a.feed(zero.data(), zero.size());
  float l[SpectrumAnalyzer::kMaxBands];
  a.levels(l);
  for (int i = 0; i < a.bands(); ++i) EXPECT_FLOAT_EQ(l[i], 0.0f);
}

TEST(SpectrumAnalyzer, LowToneLightsALowerBandThanAHighTone) {
  SpectrumAnalyzer low(24), high(24);
  const auto lo = sine(120.0, 8192), hi = sine(6000.0, 8192);
  low.feed(lo.data(), lo.size());
  high.feed(hi.data(), hi.size());
  EXPECT_LT(loudest(low), loudest(high));
  float l[SpectrumAnalyzer::kMaxBands];
  low.levels(l);
  EXPECT_GT(l[loudest(low)], 0.5f);
}

TEST(SpectrumAnalyzer, LevelsFallWhenTheSoundStops) {
  SpectrumAnalyzer a(16);
  const auto s = sine(1000.0, 8192);
  a.feed(s.data(), s.size());
  float before[SpectrumAnalyzer::kMaxBands], after[SpectrumAnalyzer::kMaxBands];
  a.levels(before);
  for (int i = 0; i < 40; ++i) a.decay();
  a.levels(after);
  const int b = loudest(a);
  EXPECT_LT(after[b], before[b] * 0.1f);
}
