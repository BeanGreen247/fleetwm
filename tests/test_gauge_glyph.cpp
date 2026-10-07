#include "bar_config.hpp"
#include "gauge_glyph.hpp"

#include <cairo.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace fleetwm {
namespace {

std::vector<uint32_t> gauge_pixels(double fraction) {
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 32, 32);
  cairo_t* cr = cairo_create(surf);
  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_paint(cr);
  kit::draw_gauge_glyph(cr, 16, 16, 24, fraction, 0.9, 0.9, 0.9);
  cairo_surface_flush(surf);
  const uint32_t* px = reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(surf));
  std::vector<uint32_t> out(px, px + 32 * (cairo_image_surface_get_stride(surf) / 4));
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return out;
}

int count_red(const std::vector<uint32_t>& v) {
  int n = 0;
  for (uint32_t p : v) {
    const int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
    if (r > g + 80 && r > b + 80) ++n;
  }
  return n;
}

TEST(GaugeGlyph, NeedleSweepsFromLowerLeftOverTheTopToLowerRight) {
  EXPECT_NEAR(kit::gauge_angle(0.0), 150.0 * M_PI / 180.0, 1e-9);
  EXPECT_NEAR(kit::gauge_angle(0.5), 270.0 * M_PI / 180.0, 1e-9);  // straight up
  EXPECT_NEAR(kit::gauge_angle(1.0), 390.0 * M_PI / 180.0, 1e-9);
  EXPECT_DOUBLE_EQ(kit::gauge_angle(-3.0), kit::gauge_angle(0.0));
  EXPECT_DOUBLE_EQ(kit::gauge_angle(7.0), kit::gauge_angle(1.0));
}

TEST(GaugeGlyph, PowerModesMapToLowMiddleAndPegged) {
  const double saver = power_mode_gauge(PowerMode::BatterySaver);
  const double balanced = power_mode_gauge(PowerMode::Normal);
  const double perf = power_mode_gauge(PowerMode::Performance);
  EXPECT_LT(saver, balanced);
  EXPECT_LT(balanced, perf);
  EXPECT_LT(saver, 0.3);
  EXPECT_DOUBLE_EQ(balanced, 0.5);
  EXPECT_DOUBLE_EQ(perf, 1.0);
  EXPECT_LT(balanced, kit::kGaugeRedFrom);  // only performance puts the needle in the red
}

TEST(GaugeGlyph, ThreeModesDrawThreeDifferentPicturesWithTheRedZoneEverywhere) {
  const auto low = gauge_pixels(power_mode_gauge(PowerMode::BatterySaver));
  const auto mid = gauge_pixels(power_mode_gauge(PowerMode::Normal));
  const auto high = gauge_pixels(power_mode_gauge(PowerMode::Performance));
  EXPECT_NE(low, mid);
  EXPECT_NE(mid, high);
  EXPECT_NE(low, high);
  EXPECT_GT(count_red(low), 0);  // the dial's red zone is always there
  EXPECT_GT(count_red(high), count_red(mid));  // the pegged needle is red too
}

}  // namespace
}  // namespace fleetwm
