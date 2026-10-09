// The glass tint engine: picking a colour from a wallpaper, mixing it into the glass colours, and the theme.toml settings.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <vector>

#include "glass_tint.hpp"
#include "test_util.hpp"
#include "theme.hpp"

using fleetwm::GlassTintConfig;
using fleetwm::GlassTintMode;
using fleetwm::kit::Color;
using fleetwm::kit::Palette;

namespace {

std::vector<uint8_t> solid(int w, int h, int r, int g, int b) {
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
  for (size_t i = 0; i < px.size(); i += 4) {
    px[i] = static_cast<uint8_t>(r);
    px[i + 1] = static_cast<uint8_t>(g);
    px[i + 2] = static_cast<uint8_t>(b);
    px[i + 3] = 255;
  }
  return px;
}

void paint(std::vector<uint8_t>& px, int w, int x0, int y0, int x1, int y1, int r, int g, int b) {
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) {
      uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 4];
      p[0] = static_cast<uint8_t>(r);
      p[1] = static_cast<uint8_t>(g);
      p[2] = static_cast<uint8_t>(b);
    }
}

double hue_of(const Color& c) {
  const double mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b}), d = mx - mn;
  if (d < 1e-9) return -1;
  double h;
  if (mx == c.r) h = 60 * std::fmod((c.g - c.b) / d, 6.0);
  else if (mx == c.g) h = 60 * ((c.b - c.r) / d + 2);
  else h = 60 * ((c.r - c.g) / d + 4);
  return h < 0 ? h + 360 : h;
}

double sat_of(const Color& c) {
  const double mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b});
  return mx <= 0 ? 0 : (mx - mn) / mx;
}

}  // namespace

TEST(GlassTint, SolidColoursKeepTheirHue) {
  const auto red = solid(8, 8, 200, 30, 30);
  const double hr = hue_of(fleetwm::kit::tint_from_pixels(red.data(), 8, 8));
  EXPECT_TRUE(hr < 12 || hr > 348) << hr;
  const auto green = solid(8, 8, 40, 190, 60);
  EXPECT_NEAR(hue_of(fleetwm::kit::tint_from_pixels(green.data(), 8, 8)), 129, 15);
  const auto blue = solid(8, 8, 30, 60, 220);
  EXPECT_NEAR(hue_of(fleetwm::kit::tint_from_pixels(blue.data(), 8, 8)), 228, 15);
}

TEST(GlassTint, ColourfulMinorityBeatsGreyMajority) {
  auto px = solid(20, 20, 128, 128, 128);  // grey sky
  paint(px, 20, 0, 14, 20, 20, 230, 110, 20);  // orange strip over less than a third of the picture
  const Color t = fleetwm::kit::tint_from_pixels(px.data(), 20, 20);
  EXPECT_NEAR(hue_of(t), 27, 14);
}

TEST(GlassTint, GreyPictureGivesAQuietGreyBlue) {
  const auto grey = solid(10, 10, 100, 100, 100);
  const Color t = fleetwm::kit::tint_from_pixels(grey.data(), 10, 10);
  EXPECT_LT(sat_of(t), 0.25);
  EXPECT_GE(std::max({t.r, t.g, t.b}), 0.35);
  const auto black = solid(10, 10, 0, 0, 0);
  EXPECT_GE(std::max({t.r, t.g, t.b}), 0.0);
  const Color k = fleetwm::kit::tint_from_pixels(black.data(), 10, 10);
  EXPECT_GE(std::max({k.r, k.g, k.b}), 0.35) << "a black wallpaper must not give black glass";
}

TEST(GlassTint, ResultStaysInGlassRange) {
  for (int v : {40, 120, 255}) {
    const auto px = solid(4, 4, v, v / 3, 0);
    const Color t = fleetwm::kit::tint_from_pixels(px.data(), 4, 4);
    const double mx = std::max({t.r, t.g, t.b});
    EXPECT_GE(mx, 0.41);
    EXPECT_LE(mx, 0.73);
    EXPECT_GE(sat_of(t), 0.34);
    EXPECT_LE(sat_of(t), 0.81);
  }
}

TEST(GlassTint, NothingToReadFallsBackToAeroBlue) {
  const Color a = fleetwm::kit::aero_blue();
  const Color t = fleetwm::kit::tint_from_pixels(nullptr, 0, 0);
  EXPECT_DOUBLE_EQ(t.r, a.r);
  EXPECT_DOUBLE_EQ(t.b, a.b);
}

TEST(GlassTint, HueNearTheWrapAroundIsAveragedNotCancelled) {
  auto px = solid(10, 10, 220, 30, 60);  // hue ~ 349
  paint(px, 10, 0, 0, 10, 5, 220, 50, 20);  // hue ~ 10
  const double h = hue_of(fleetwm::kit::tint_from_pixels(px.data(), 10, 10));
  EXPECT_TRUE(h < 25 || h > 335) << h;
}

TEST(GlassTint, ThemeModeKeepsTheThemeColours) {
  Palette pal;
  GlassTintConfig cfg;
  cfg.mode = GlassTintMode::Theme;
  fleetwm::kit::apply_glass_tint(pal, cfg);
  EXPECT_DOUBLE_EQ(pal.glass_surface.r, pal.bg_primary.r);
  EXPECT_DOUBLE_EQ(pal.glass_title_idle.b, pal.bg_secondary.b);
  EXPECT_NEAR(pal.glass_title.r, pal.bg_secondary.r + (pal.accent.r - pal.bg_secondary.r) * 0.12, 1e-9);
}

TEST(GlassTint, CustomColourPullsTheGlassTowardsIt) {
  Palette pal;
  GlassTintConfig cfg;
  cfg.mode = GlassTintMode::Custom;
  cfg.hex = "#ff0000";
  cfg.intensity = 100;
  fleetwm::kit::apply_glass_tint(pal, cfg);
  EXPECT_GT(pal.glass_title.r, pal.glass_title.b + 0.4);
  EXPECT_GT(pal.glass_title.r, pal.glass_title_idle.r);  // the focused bar is the more tinted one
}

TEST(GlassTint, IntensityScalesTheTint) {
  Palette lo, hi;
  GlassTintConfig cfg;
  cfg.mode = GlassTintMode::Custom;
  cfg.hex = "#00ff00";
  cfg.intensity = 0;
  fleetwm::kit::apply_glass_tint(lo, cfg);
  cfg.intensity = 100;
  fleetwm::kit::apply_glass_tint(hi, cfg);
  EXPECT_GT(hi.glass_title.g, lo.glass_title.g);
  EXPECT_GT(hi.glass_surface.g, lo.glass_surface.g);
  EXPECT_GT(lo.glass_title.g, Palette{}.bg_secondary.g);  // even 0 shows a hint of the colour
}

TEST(GlassTint, BadColourFallsBackToAero) {
  Palette a, b;
  GlassTintConfig cfg;
  cfg.mode = GlassTintMode::Custom;
  cfg.hex = "not a colour";
  fleetwm::kit::apply_glass_tint(a, cfg);
  cfg.mode = GlassTintMode::Aero;
  fleetwm::kit::apply_glass_tint(b, cfg);
  EXPECT_DOUBLE_EQ(a.glass_title.b, b.glass_title.b);
}

TEST(GlassTintNames, RoundTripAndUnknownIsAero) {
  for (auto m : {GlassTintMode::Aero, GlassTintMode::Custom, GlassTintMode::Wallpaper, GlassTintMode::Theme})
    EXPECT_EQ(fleetwm::glass_tint_mode_from_string(fleetwm::glass_tint_mode_to_string(m)), m);
  EXPECT_EQ(fleetwm::glass_tint_mode_from_string("???"), GlassTintMode::Aero);
}

class GlassTintFile : public fleetwm::testutil::ScopedConfigHome {};

TEST_F(GlassTintFile, DefaultsAreAeroBlue) {
  const auto c = fleetwm::load_theme_config().glass_tint;
  EXPECT_EQ(c.mode, GlassTintMode::Aero);
  EXPECT_EQ(c.intensity, 60);
}

TEST_F(GlassTintFile, SavedAndLoaded) {
  fleetwm::ThemeConfig t;
  t.glass_tint.mode = GlassTintMode::Wallpaper;
  t.glass_tint.hex = "#112233";
  t.glass_tint.intensity = 85;
  fleetwm::save_theme_config(t);
  const auto c = fleetwm::load_theme_config().glass_tint;
  EXPECT_EQ(c.mode, GlassTintMode::Wallpaper);
  EXPECT_EQ(c.hex, "#112233");
  EXPECT_EQ(c.intensity, 85);
}

TEST_F(GlassTintFile, MalformedValuesAreIgnoredOrClamped) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream(dir_ / "fleetwm" / "theme.toml") << "[glass_tint]\nmode = \"custom\"\ncolor = \"banana\"\nintensity = 900\n";
  const auto c = fleetwm::load_theme_config().glass_tint;
  EXPECT_EQ(c.mode, GlassTintMode::Custom);
  EXPECT_EQ(c.hex, "#4a86c8");
  EXPECT_EQ(c.intensity, 100);
}
