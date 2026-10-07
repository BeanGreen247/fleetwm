#include "network_glyphs.hpp"

#include <cairo.h>
#include <gtest/gtest.h>

#include <cstdint>

namespace fleetwm::net {
namespace {

struct GlyphCounts {
  int green = 0, blue = 0, red = 0, grey = 0;
};

GlyphCounts glyph_render(void (*draw)(cairo_t*)) {
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 48, 48);
  cairo_t* cr = cairo_create(surf);
  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_paint(cr);
  draw(cr);
  cairo_surface_flush(surf);
  const uint32_t* px = reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(surf));
  const int stride = cairo_image_surface_get_stride(surf) / 4;
  GlyphCounts c;
  for (int y = 0; y < 48; ++y)
    for (int x = 0; x < 48; ++x) {
      const uint32_t v = px[y * stride + x];
      const int r = (v >> 16) & 255, g = (v >> 8) & 255, b = v & 255;
      if (g > r + 60 && g > b + 60) ++c.green;
      else if (b > r + 60 && b > g + 20) ++c.blue;
      else if (r > g + 80 && r > b + 80) ++c.red;
      else if (r > 40 && g > 40 && b > 40) ++c.grey;
    }
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return c;
}

const GlyphColor kGlyphWhite{0.9, 0.9, 0.9, 1.0};

template <int Lit>
void glyph_wifi(cairo_t* cr) { draw_wifi_glyph(cr, 24, 24, 32, Lit, kGlyphWhite); }
template <int Lit>
void glyph_mobile(cairo_t* cr) { draw_mobile_glyph(cr, 24, 24, 32, Lit, kGlyphWhite); }
template <bool Lit>
void glyph_wired(cairo_t* cr) { draw_ethernet_glyph(cr, 24, 24, 32, Lit, kGlyphWhite); }
void glyph_wifi_crossed(cairo_t* cr) { draw_wifi_glyph(cr, 24, 24, 32, 0, kGlyphWhite, true); }

TEST(NetworkGlyphs, WifiBarsFollowTheStrength) {
  EXPECT_EQ(wifi_bars_lit(0), 0);
  EXPECT_EQ(wifi_bars_lit(1), 1);
  EXPECT_EQ(wifi_bars_lit(19), 1);
  EXPECT_EQ(wifi_bars_lit(20), 2);
  EXPECT_EQ(wifi_bars_lit(40), 3);
  EXPECT_EQ(wifi_bars_lit(60), 4);
  EXPECT_EQ(wifi_bars_lit(80), 5);
  EXPECT_EQ(wifi_bars_lit(100), 5);
}

TEST(NetworkGlyphs, WifiLitBarsAreGreenAndMoreBarsMeanMoreGreen) {
  EXPECT_EQ(glyph_render(glyph_wifi<0>).green, 0);
  const int one = glyph_render(glyph_wifi<1>).green, three = glyph_render(glyph_wifi<3>).green, five = glyph_render(glyph_wifi<5>).green;
  EXPECT_GT(one, 0);
  EXPECT_GT(three, one);
  EXPECT_GT(five, three);
  EXPECT_GT(glyph_render(glyph_wifi<0>).grey, 0);  // the unlit bars are still drawn, faintly
}

TEST(NetworkGlyphs, MobileHasItsOwnMastAndGreenBars) {
  EXPECT_EQ(glyph_render(glyph_mobile<0>).green, 0);
  EXPECT_GT(glyph_render(glyph_mobile<4>).green, glyph_render(glyph_mobile<1>).green);
  EXPECT_GT(glyph_render(glyph_mobile<0>).grey, 0);
}

TEST(NetworkGlyphs, WiredIsABlueScreenWithCableOnlyWhenConnected) {
  EXPECT_GT(glyph_render(glyph_wired<true>).blue, 0);
  EXPECT_EQ(glyph_render(glyph_wired<false>).blue, 0);
  EXPECT_GT(glyph_render(glyph_wired<false>).grey, 0);
}

TEST(NetworkGlyphs, CrossedAddsARedSlash) {
  EXPECT_EQ(glyph_render(glyph_wifi<0>).red, 0);
  EXPECT_GT(glyph_render(glyph_wifi_crossed).red, 0);
}

TEST(NetworkGlyphs, ListSignalBarsStillFourSteps) {
  EXPECT_EQ(signal_bars_lit(0), 0);
  EXPECT_EQ(signal_bars_lit(24), 1);
  EXPECT_EQ(signal_bars_lit(75), 4);
}

}  // namespace
}  // namespace fleetwm::net
