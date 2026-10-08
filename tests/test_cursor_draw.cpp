#include "cursor_draw.hpp"

#include <cairo.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fleetwm {
namespace {

using kit::CursorShape;

struct CdPicture {
  int size;
  std::vector<uint32_t> px;
  int alpha_at(int x, int y) const { return px[static_cast<size_t>(y) * size + x] >> 24; }
  int covered(int min_alpha = 1) const {
    int n = 0;
    for (uint32_t p : px) n += static_cast<int>(p >> 24) >= min_alpha;
    return n;
  }
};

CdPicture cd_draw(CursorShape shape, bool glass, int scale, int phase = 0) {
  const int size = kit::kCursorGrid * scale;
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
  cairo_t* cr = cairo_create(s);
  kit::draw_cursor(cr, shape, glass, scale, phase);
  cairo_destroy(cr);
  cairo_surface_flush(s);
  const uint32_t* p = reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(s));
  CdPicture out{size, std::vector<uint32_t>(p, p + static_cast<size_t>(size) * (cairo_image_surface_get_stride(s) / 4))};
  cairo_surface_destroy(s);
  return out;
}

constexpr int kShapes = static_cast<int>(CursorShape::Count);

TEST(CursorDraw, EveryShapeDrawsSomethingInBothStylesAndAtBothScales) {
  for (int i = 0; i < kShapes; ++i)
    for (bool glass : {false, true})
      for (int scale : {1, 2}) {
        const CdPicture pic = cd_draw(static_cast<CursorShape>(i), glass, scale);
        EXPECT_GT(pic.covered(), 40 * scale * scale) << "shape " << i << (glass ? " glass" : " flat") << " scale " << scale;
        EXPECT_LT(pic.covered(), pic.size * pic.size * 3 / 4) << "a cursor is not a filled square";
      }
}

TEST(CursorDraw, BusyRingTurnsWithThePhaseAndNothingElseDoes) {
  for (CursorShape shape : {CursorShape::Wait, CursorShape::Progress})
    for (bool glass : {false, true}) {
      EXPECT_TRUE(kit::cursor_shape_spins(shape));
      const CdPicture a = cd_draw(shape, glass, 2, 0), b = cd_draw(shape, glass, 2, 3), c = cd_draw(shape, glass, 2, kit::kBusyFrames);
      EXPECT_NE(a.px, b.px) << (glass ? "glass" : "flat") << " ring looked the same a quarter turn later";
      EXPECT_EQ(a.px, c.px) << "a full turn must come back to the first picture";
      EXPECT_EQ(a.covered(), b.covered()) << "the ring keeps its outline";
    }
  for (int i = 0; i < kShapes; ++i) {
    const CursorShape shape = static_cast<CursorShape>(i);
    if (kit::cursor_shape_spins(shape)) continue;
    EXPECT_EQ(cd_draw(shape, true, 1, 0).px, cd_draw(shape, true, 1, 5).px) << "shape " << i << " must ignore the phase";
  }
}

TEST(CursorDraw, ScaleTwoIsTheSameDrawingAtDoubleResolution) {
  for (int i = 0; i < kShapes; ++i) {
    const CdPicture one = cd_draw(static_cast<CursorShape>(i), true, 1), two = cd_draw(static_cast<CursorShape>(i), true, 2);
    const double ratio = static_cast<double>(two.covered()) / one.covered();
    EXPECT_GT(ratio, 3.3) << "shape " << i;
    EXPECT_LT(ratio, 4.7) << "shape " << i;
  }
}

TEST(CursorDraw, GlassAndFlatDifferAndGlassHasTheSoftShadow) {
  for (int i = 0; i < kShapes; ++i) {
    const CdPicture glass = cd_draw(static_cast<CursorShape>(i), true, 1), flat = cd_draw(static_cast<CursorShape>(i), false, 1);
    EXPECT_NE(glass.px, flat.px) << "shape " << i;
  }
  // The pointer shadow: translucent pixels around the arrow that the flat one does not have.
  const CdPicture glass = cd_draw(CursorShape::Arrow, true, 2), flat = cd_draw(CursorShape::Arrow, false, 2);
  int soft_glass = 0, soft_flat = 0;
  for (uint32_t p : glass.px) soft_glass += (p >> 24) > 8 && (p >> 24) < 90;
  for (uint32_t p : flat.px) soft_flat += (p >> 24) > 8 && (p >> 24) < 90;
  EXPECT_GT(soft_glass, 2 * soft_flat + 20);
}

TEST(CursorDraw, FlatPointerIsSolidWhiteWithABlackOutline) {
  const CdPicture flat = cd_draw(CursorShape::Arrow, false, 4);
  int white = 0, black = 0, other = 0;
  for (uint32_t p : flat.px) {
    if ((p >> 24) != 255) continue;
    const int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
    if (r > 245 && g > 245 && b > 245) ++white;
    else if (r < 12 && g < 12 && b < 12) ++black;
    else ++other;
  }
  EXPECT_GT(white, 300);
  EXPECT_GT(black, 100);
  EXPECT_LT(other, white / 2) << "only anti-aliased edge pixels are neither white nor black";
  // inside the body, top and bottom alike, the fill is the same pure white (no gradient)
  EXPECT_EQ(flat.px[static_cast<size_t>(4 * 6) * flat.size + 4 * 2], 0xFFFFFFFFu);
  EXPECT_EQ(flat.px[static_cast<size_t>(4 * 11) * flat.size + 4 * 3], 0xFFFFFFFFu);
}

TEST(CursorDraw, GlassBodyIsAGradientFromWhiteToPaleBlueGrey) {
  const CdPicture glass = cd_draw(CursorShape::UpArrow, true, 4);
  auto lum = [&](int x, int y) {
    const uint32_t p = glass.px[static_cast<size_t>(y) * glass.size + x];
    return int((p >> 16) & 255) + int((p >> 8) & 255) + int(p & 255);
  };
  EXPECT_GT(lum(64, 4 * 11), lum(64, 4 * 25)) << "lighter at the top of the body than at the bottom";
}

TEST(CursorDraw, HotspotsAreInsideTheGridAndOnTheShapeForTheTipShapes) {
  for (int i = 0; i < kShapes; ++i) {
    const kit::CursorHotspot h = kit::cursor_hotspot(static_cast<CursorShape>(i));
    EXPECT_GE(h.x, 0);
    EXPECT_GE(h.y, 0);
    EXPECT_LT(h.x, kit::kCursorGrid);
    EXPECT_LT(h.y, kit::kCursorGrid);
  }
  for (CursorShape tip : {CursorShape::Arrow, CursorShape::Help, CursorShape::Progress, CursorShape::Hand, CursorShape::UpArrow}) {
    const CdPicture pic = cd_draw(tip, false, 1);
    const kit::CursorHotspot h = kit::cursor_hotspot(tip);
    int near = 0;
    for (int dy = -2; dy <= 2; ++dy)
      for (int dx = -2; dx <= 2; ++dx)
        if (h.x + dx >= 0 && h.y + dy >= 0) near += pic.alpha_at(h.x + dx, h.y + dy) > 0;
    EXPECT_GT(near, 0) << "the tip of shape " << static_cast<int>(tip) << " is where the pointer says it is";
  }
}

TEST(CursorDraw, NamesFromWaylandClientsAndXcursorThemesMapToTheRightShapes) {
  struct Case {
    const char* name;
    CursorShape shape;
  };
  const Case cases[] = {{"left_ptr", CursorShape::Arrow}, {"default", CursorShape::Arrow}, {"text", CursorShape::Text},
                        {"xterm", CursorShape::Text}, {"pointer", CursorShape::Hand}, {"hand2", CursorShape::Hand},
                        {"wait", CursorShape::Wait}, {"progress", CursorShape::Progress}, {"help", CursorShape::Help},
                        {"not-allowed", CursorShape::NotAllowed}, {"crosshair", CursorShape::Cross},
                        {"all-scroll", CursorShape::Move}, {"ew-resize", CursorShape::ResizeEW}, {"e-resize", CursorShape::ResizeEW},
                        {"ns-resize", CursorShape::ResizeNS}, {"s-resize", CursorShape::ResizeNS},
                        {"nesw-resize", CursorShape::ResizeNESW}, {"ne-resize", CursorShape::ResizeNESW},
                        {"nwse-resize", CursorShape::ResizeNWSE}, {"se-resize", CursorShape::ResizeNWSE},
                        {"nw-resize", CursorShape::ResizeNWSE}, {"sw-resize", CursorShape::ResizeNESW}};
  for (const Case& c : cases) {
    CursorShape got = CursorShape::Count;
    EXPECT_TRUE(kit::cursor_shape_for_name(c.name, &got)) << c.name;
    EXPECT_EQ(got, c.shape) << c.name;
  }
  CursorShape unused;
  EXPECT_FALSE(kit::cursor_shape_for_name("some-theme-specific-name", &unused));
  EXPECT_FALSE(kit::cursor_shape_for_name("", &unused));
  EXPECT_FALSE(kit::cursor_shape_for_name(nullptr, &unused));
}

TEST(CursorDraw, EveryShapeIsReachableByAName) {
  bool seen[kShapes] = {};
  for (const char* name : {"left_ptr", "help", "progress", "wait", "text", "pointer", "crosshair", "not-allowed", "all-scroll",
                           "ew-resize", "ns-resize", "nesw-resize", "nwse-resize", "up_arrow"}) {
    CursorShape s;
    ASSERT_TRUE(kit::cursor_shape_for_name(name, &s)) << name;
    seen[static_cast<int>(s)] = true;
  }
  for (int i = 0; i < kShapes; ++i) EXPECT_TRUE(seen[i]) << "shape " << i << " has no name";
}

TEST(CursorCache, DrawsEachPictureOnceAndThenReusesIt) {
  kit::CursorCache<int> cache;
  int made = 0;
  auto make = [&] { return ++made; };
  const kit::CursorKey a{CursorShape::Arrow, true, 1}, b{CursorShape::Arrow, false, 1}, c{CursorShape::Arrow, true, 2},
      d{CursorShape::Text, true, 1};
  EXPECT_EQ(cache.get(a, make), 1);
  EXPECT_EQ(cache.get(a, make), 1);
  EXPECT_EQ(cache.get(a, make), 1);
  EXPECT_EQ(cache.get(b, make), 2) << "flat is its own picture";
  EXPECT_EQ(cache.get(c, make), 3) << "so is scale 2";
  EXPECT_EQ(cache.get(d, make), 4);
  EXPECT_EQ(made, 4);
  EXPECT_EQ(cache.entries(), 4);
  EXPECT_EQ(cache.hits(), 2);
  EXPECT_EQ(cache.misses(), 4);
}

TEST(CursorCache, ABufferThatCouldNotBeMadeIsNotKeptSoTheNextCallTriesAgain) {
  kit::CursorCache<int> cache;
  int tries = 0;
  EXPECT_EQ(cache.get({CursorShape::Wait, true, 1}, [&] { ++tries; return 0; }), 0);
  EXPECT_EQ(cache.entries(), 0);
  EXPECT_EQ(cache.get({CursorShape::Wait, true, 1}, [&] { ++tries; return 7; }), 7);
  EXPECT_EQ(tries, 2);
  EXPECT_EQ(cache.entries(), 1);
}

TEST(CursorCache, ClearReleasesEveryKeptPicture) {
  kit::CursorCache<int> cache;
  for (int i = 0; i < kShapes; ++i) cache.get({static_cast<CursorShape>(i), true, 1}, [&] { return i + 1; });
  int released = 0, sum = 0;
  cache.clear([&](int v) { ++released; sum += v; });
  EXPECT_EQ(released, kShapes);
  EXPECT_EQ(sum, kShapes * (kShapes + 1) / 2);
  EXPECT_EQ(cache.entries(), 0);
}

TEST(CursorCache, WholeSetFitsInAFewHundredKilobytes) {
  // 14 shapes x 2 styles x scales 1 and 2: 32 x 32 x 4 bytes at scale 1, four times that at scale 2.
  const size_t bytes = kShapes * 2 * (32 * 32 * 4 + 64 * 64 * 4);
  EXPECT_LT(bytes, 700u * 1024);
}

std::string cd_read(const char* rel) {
  std::ifstream in(std::string(FLEETWM_SOURCE_DIR) + "/" + rel);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

TEST(CursorCompositor, DrawsItsOwnPointerFromTheCacheAndRebuildsItWhenGlassChanges) {
  const std::string server = cd_read("src/compositor/server.cpp");
  EXPECT_NE(server.find("kit::cursor_shape_for_name(name, &shape)"), std::string::npos);
  EXPECT_NE(server.find("cursor_picture(shape, theme_config_.glass, scale, 0)"), std::string::npos);
  const size_t reload = server.find("void Server::reload_theme_config()");
  ASSERT_NE(reload, std::string::npos);
  EXPECT_NE(server.find("refresh_cursor();", reload), std::string::npos) << "a glass toggle must redraw the pointer";
  const std::string cursor = cd_read("src/compositor/cursor.cpp");
  EXPECT_NE(cursor.find("kit::draw_cursor("), std::string::npos);
  EXPECT_NE(cursor.find("cursor_picture_cache().get("), std::string::npos);
}

}  // namespace
}  // namespace fleetwm
