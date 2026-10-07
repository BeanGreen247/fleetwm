#include "glyph_cache.hpp"

#include <cairo.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gauge_glyph.hpp"
#include "network_glyphs.hpp"
#include "speaker_glyph.hpp"

namespace fleetwm {
namespace {

struct GcImage {
  cairo_surface_t* s;
  GcImage() : s(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64)) {}
  ~GcImage() { cairo_surface_destroy(s); }
  std::vector<uint32_t> pixels() {
    cairo_surface_flush(s);
    const uint32_t* p = reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(s));
    return {p, p + 64 * (cairo_image_surface_get_stride(s) / 4)};
  }
};

void gc_gauge(cairo_t* g, double x, double y) { kit::draw_gauge_glyph(g, x, y, 16, 0.5, 0.9, 0.9, 0.9); }

TEST(GlyphCache, CachedPictureEqualsDrawingTheIconDirectly) {
  kit::glyph_cache_clear();
  GcImage cached, direct;
  cairo_t* c1 = cairo_create(cached.s);
  kit::draw_cached_glyph(c1, 20, 20, {1, 50, kit::glyph_rgb(0.9, 0.9, 0.9), 24, 24}, gc_gauge);
  cairo_destroy(c1);
  cairo_t* c2 = cairo_create(direct.s);
  gc_gauge(c2, 20 + 12.0, 20 + 12.0);  // the cached picture is centred in its 24 px square
  cairo_destroy(c2);
  EXPECT_EQ(cached.pixels(), direct.pixels());
}

TEST(GlyphCache, SecondDrawIsACopyAndNeverCallsTheDrawFunction) {
  kit::glyph_cache_clear();
  int calls = 0;
  GcImage img;
  cairo_t* cr = cairo_create(img.s);
  const kit::GlyphKey key{2, 3, kit::glyph_rgb(1, 1, 1), 24, 24};
  for (int i = 0; i < 5; ++i) kit::draw_cached_glyph(cr, 10, 10, key, [&](cairo_t* g, double x, double y) {
    ++calls;
    kit::draw_speaker_glyph(g, x, y, 16, 3, 1, 1, 1);
  });
  cairo_destroy(cr);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(kit::glyph_cache_stats().hits, 4);
  EXPECT_EQ(kit::glyph_cache_stats().misses, 1);
}

TEST(GlyphCache, EveryDifferentStateColourOrSizeGetsItsOwnPicture) {
  kit::glyph_cache_clear();
  GcImage img;
  cairo_t* cr = cairo_create(img.s);
  auto draw = [](cairo_t*, double, double) {};
  kit::draw_cached_glyph(cr, 0, 0, {1, 0, 1, 24, 24}, draw);
  kit::draw_cached_glyph(cr, 0, 0, {1, 1, 1, 24, 24}, draw);
  kit::draw_cached_glyph(cr, 0, 0, {1, 0, 2, 24, 24}, draw);
  kit::draw_cached_glyph(cr, 0, 0, {2, 0, 1, 24, 24}, draw);
  kit::draw_cached_glyph(cr, 0, 0, {1, 0, 1, 26, 24}, draw);
  cairo_destroy(cr);
  EXPECT_EQ(kit::glyph_cache_stats().entries, 5);
}

TEST(GlyphCache, StaysBoundedWhateverStatesAreDrawn) {
  kit::glyph_cache_clear();
  GcImage img;
  cairo_t* cr = cairo_create(img.s);
  for (int i = 0; i < 1000; ++i) kit::draw_cached_glyph(cr, 0, 0, {6, i, 1, 32, 18}, [](cairo_t*, double, double) {});
  cairo_destroy(cr);
  EXPECT_LE(kit::glyph_cache_stats().entries, 96);
  // the 96 slots are 32x18 pictures: well under half a megabyte
  EXPECT_LE(kit::glyph_cache_stats().entries * 32 * 18 * 4, 512 * 1024);
}

TEST(GlyphCache, EvictsTheLeastRecentlyUsedPictureFirst) {
  kit::glyph_cache_clear();
  GcImage img;
  cairo_t* cr = cairo_create(img.s);
  int calls = 0;
  auto draw = [&](cairo_t*, double, double) { ++calls; };
  for (int i = 0; i < 96; ++i) kit::draw_cached_glyph(cr, 0, 0, {9, i, 1, 8, 8}, draw);
  kit::draw_cached_glyph(cr, 0, 0, {9, 0, 1, 8, 8}, draw);  // touch the oldest
  kit::draw_cached_glyph(cr, 0, 0, {9, 1000, 1, 8, 8}, draw);  // one more: evicts state 1, not 0
  const int before = calls;
  kit::draw_cached_glyph(cr, 0, 0, {9, 0, 1, 8, 8}, draw);
  EXPECT_EQ(calls, before) << "the recently used picture is still cached";
  kit::draw_cached_glyph(cr, 0, 0, {9, 1, 1, 8, 8}, draw);
  EXPECT_EQ(calls, before + 1) << "the least recently used one was dropped";
  cairo_destroy(cr);
}

TEST(GlyphCache, ColourKeyRoundsChannelsToEightBits) {
  EXPECT_EQ(kit::glyph_rgb(1, 1, 1), 0xffffffu);
  EXPECT_EQ(kit::glyph_rgb(0, 0, 0), 0u);
  EXPECT_EQ(kit::glyph_rgb(1, 0, 0), 0xff0000u);
  EXPECT_EQ(kit::glyph_rgb(2.0, -1.0, 0.5), 0xff0080u);
}

TEST(GlyphCache, TheBarDrawsEveryCustomIconThroughTheCache) {
  // read the bar source: a direct draw_*_glyph call outside the cache wrappers would bring the per-repaint cost back
  std::ifstream in(std::string(FLEETWM_SOURCE_DIR) + "/src/bar/main.cpp");
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string src = ss.str();
  EXPECT_EQ(src.find("draw_gauge_glyph(cr"), std::string::npos);
  EXPECT_EQ(src.find("draw_speaker_glyph(cr"), std::string::npos);
  EXPECT_EQ(src.find("draw_wifi_glyph(cr"), std::string::npos);
  EXPECT_EQ(src.find("draw_ethernet_glyph(cr"), std::string::npos);
  EXPECT_NE(src.find("draw_cached_glyph("), std::string::npos);
}

}  // namespace
}  // namespace fleetwm
