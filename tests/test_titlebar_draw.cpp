// Titlebar drawing and its caches. A titlebar is redrawn whenever the pointer enters or leaves a button, so
// the glass (Windows Aero) background and the caption strip are kept as finished pictures. These tests check
// that the cached picture is the same as drawing from scratch, that the caches stay bounded and are really
// used, that glass and matte differ only in the background, and that the Settings row is named after Aero.
#include <gtest/gtest.h>

#include <cairo.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>

#include "caption_buttons.hpp"
#include "titlebar_draw.hpp"

namespace {

using namespace fleetwm;
namespace td = fleetwm::kit;

struct TdImage {
  cairo_surface_t* surf = nullptr;
  explicit TdImage(int w, int h) { surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h); }
  ~TdImage() { cairo_surface_destroy(surf); }
  TdImage(const TdImage&) = delete;
  const unsigned char* data() {
    cairo_surface_flush(surf);
    return cairo_image_surface_get_data(surf);
  }
  int stride() const { return cairo_image_surface_get_stride(surf); }
  int w() const { return cairo_image_surface_get_width(surf); }
  int h() const { return cairo_image_surface_get_height(surf); }
};

geom::TitlebarMetrics td_metrics() {
  geom::TitlebarMetrics m;
  m.height = 32;
  m.button_w = 38;
  m.button_h = 24;
  m.strip = true;
  return m;
}

td::TitlebarPaint td_paint(int width, bool glass, bool focused = true) {
  td::TitlebarPaint p;
  p.title = "Fleetwm Settings";
  p.focused = focused;
  p.glass = glass;
  p.layout = geom::layout_titlebar(width, td_metrics());
  return p;
}

void td_draw(TdImage& img, const td::TitlebarPaint& p) {
  cairo_t* cr = cairo_create(img.surf);
  td::draw_titlebar(cr, img.w(), img.h(), p, td::Palette{});
  cairo_destroy(cr);
}

int td_max_diff(TdImage& a, TdImage& b, int x0 = 0, int x1 = -1, int y0 = 0, int y1 = -1) {
  const unsigned char *da = a.data(), *db = b.data();
  int worst = 0;
  if (x1 < 0) x1 = a.w();
  if (y1 < 0) y1 = a.h();
  for (int y = y0; y < y1; ++y)
    for (int x = x0 * 4; x < x1 * 4; ++x) worst = std::max(worst, std::abs(int(da[y * a.stride() + x]) - int(db[y * b.stride() + x])));
  return worst;
}

std::string td_read(const char* rel) {
  std::ifstream in(std::string(FLEETWM_SOURCE_DIR) + "/" + rel);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

double td_now_us() { return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void td_clear() {
  td::titlebar_glass_cache_clear();
  td::titlebar_strip_cache_clear();
}

}  // namespace

TEST(TitlebarDraw, CachedGlassBackgroundIsThePictureDrawnFromScratch) {
  td_clear();
  TdImage fresh(800, 32), cached(800, 32), again(800, 32);
  const td::Color base{0.12, 0.13, 0.20, 1};
  { cairo_t* cr = cairo_create(fresh.surf); td::paint_titlebar_glass_background(cr, 800, 32, true, base); cairo_destroy(cr); }  // first call draws and stores
  { cairo_t* cr = cairo_create(cached.surf); td::paint_titlebar_glass_background(cr, 800, 32, true, base); cairo_destroy(cr); }  // second call copies
  { td_clear(); cairo_t* cr = cairo_create(again.surf); td::paint_titlebar_glass_background(cr, 800, 32, true, base); cairo_destroy(cr); }
  EXPECT_EQ(td_max_diff(fresh, cached), 0) << "the copy must equal the drawing";
  EXPECT_EQ(td_max_diff(fresh, again), 0) << "and the drawing must be repeatable";
}

TEST(TitlebarDraw, CachedStripMatchesDirectDrawingInEveryState) {
  const int W = 800;
  const geom::TitlebarLayout layout = geom::layout_titlebar(W, td_metrics());
  td::CaptionButton strip[4];
  for (int i = 0; i < layout.count; ++i) strip[i] = {layout.buttons[i].id, layout.buttons[i].x, layout.buttons[i].y, layout.buttons[i].w, layout.buttons[i].h};
  int checked = 0;
  for (int hover : {-1, static_cast<int>(geom::kBtnPin), static_cast<int>(geom::kBtnMinimize), static_cast<int>(geom::kBtnMaximize), static_cast<int>(geom::kBtnClose)})
    for (int focused = 0; focused < 2; ++focused)
      for (int maximized = 0; maximized < 2; ++maximized)
        for (int pinned = 0; pinned < 2; ++pinned) {
          td_clear();
          td::TitlebarPaint p = td_paint(W, false, focused);
          p.maximized = maximized;
          p.pinned = pinned;
          p.hover_id = hover;
          p.title = "";  // the strip only
          TdImage cached(W, 32), direct(W, 32);
          td_draw(cached, p);  // fills the cache
          TdImage second(W, 32);
          td_draw(second, p);  // served from it
          {
            cairo_t* cr = cairo_create(direct.surf);
            // what the matte background and then the strip produce, drawn without any cache
            const td::Palette pal;
            const td::Color bg = focused ? td::Color{pal.bg_secondary.r + (pal.accent.r - pal.bg_secondary.r) * 0.12, pal.bg_secondary.g + (pal.accent.g - pal.bg_secondary.g) * 0.12,
                                                     pal.bg_secondary.b + (pal.accent.b - pal.bg_secondary.b) * 0.12, 1.0}
                                         : pal.bg_secondary;
            td::set_source(cr, bg);
            cairo_paint(cr);
            td::Color line{bg.r + (pal.fg_primary.r - bg.r) * 0.12, bg.g + (pal.fg_primary.g - bg.g) * 0.12, bg.b + (pal.fg_primary.b - bg.b) * 0.12, 1.0};
            td::set_source(cr, line);
            cairo_rectangle(cr, 0, 31, W, 1);
            cairo_fill(cr);
            td::CaptionState st;
            st.focused = focused;
            st.maximized = maximized;
            st.pinned = pinned;
            st.hover_id = hover;
            td::draw_caption_buttons(cr, strip, layout.count, st);
            cairo_destroy(cr);
          }
          EXPECT_LE(td_max_diff(cached, direct), 2) << "hover " << hover << " focused " << focused << " max " << maximized << " pin " << pinned;
          EXPECT_EQ(td_max_diff(cached, second), 0) << "a cache hit must give the same pixels as the miss";
          ++checked;
        }
  EXPECT_EQ(checked, 40);
}

TEST(TitlebarDraw, RepeatedRedrawsHitTheCachesInsteadOfGrowingThem) {
  td_clear();
  TdImage img(800, 32);
  td::TitlebarPaint p = td_paint(800, true);
  for (int i = 0; i < 100; ++i) {
    p.hover_id = (i % 5) - 1;  // the pointer moves across the buttons, again and again
    td_draw(img, p);
  }
  EXPECT_EQ(td::titlebar_glass_cache_stats().entries, 1) << "one glass background for one size, focus and colour";
  EXPECT_EQ(td::titlebar_strip_cache_stats().entries, 5) << "one strip picture per button state, however often it is redrawn";
}

TEST(TitlebarDraw, DifferentSizesFocusAndColoursGetTheirOwnBackground) {
  td_clear();
  TdImage a(800, 32), b(900, 32), c(800, 32);
  td_draw(a, td_paint(800, true, true));
  td_draw(b, td_paint(900, true, true));
  td_draw(c, td_paint(800, true, false));
  EXPECT_EQ(td::titlebar_glass_cache_stats().entries, 3);
  TdImage x(800, 32), y(800, 32);
  { cairo_t* cr = cairo_create(x.surf); td::paint_titlebar_glass_background(cr, 800, 32, true, {0.1, 0.1, 0.1, 1}); cairo_destroy(cr); }
  { cairo_t* cr = cairo_create(y.surf); td::paint_titlebar_glass_background(cr, 800, 32, true, {0.6, 0.2, 0.1, 1}); cairo_destroy(cr); }
  EXPECT_GT(td_max_diff(x, y), 20) << "another theme colour must not be served the old picture";
}

TEST(TitlebarDraw, CachesStayWithinTheirByteBudgetWhateverIsOpened) {
  td_clear();
  for (int w = 300; w < 1900; w += 7) {  // a window of every width
    TdImage img(w, 32);
    td::TitlebarPaint p = td_paint(w, true);
    p.hover_id = w % 3 ? geom::kBtnClose : geom::kBtnMinimize;
    td_draw(img, p);
  }
  EXPECT_LE(td::titlebar_glass_cache_stats().bytes, size_t(2) << 20);
  EXPECT_LE(td::titlebar_strip_cache_stats().bytes, size_t(1) << 20);
  EXPECT_GT(td::titlebar_glass_cache_stats().entries, 3) << "recent ones are kept";
  // an absurdly wide bar is drawn but not kept
  TdImage huge(9000, 32);
  td_draw(huge, td_paint(9000, true));
  EXPECT_LE(td::titlebar_glass_cache_stats().bytes, size_t(2) << 20);
}

TEST(TitlebarDraw, GlassAndMatteDifferOnlyInTheBackground) {
  td_clear();
  TdImage matte(800, 32), glass(800, 32);
  td::TitlebarPaint p = td_paint(800, false);
  td_draw(matte, p);
  p.glass = true;
  td_draw(glass, p);
  const unsigned char* m = matte.data();
  const unsigned char* g = glass.data();
  // matte is opaque everywhere, glass is translucent where only the background is
  EXPECT_EQ(m[10 * matte.stride() + 20 * 4 + 3], 255);
  EXPECT_LT(g[10 * glass.stride() + 20 * 4 + 3], 230) << "the Aero background lets the desktop show through";
  EXPECT_GT(g[10 * glass.stride() + 20 * 4 + 3], 100);
  // the caption strip (solid glass buttons) is the same in both
  const geom::TitlebarLayout layout = geom::layout_titlebar(800, td_metrics());
  const int x0 = static_cast<int>(layout.buttons[0].x) + 4, x1 = static_cast<int>(layout.buttons[3].x + layout.buttons[3].w) - 4;
  EXPECT_LE(td_max_diff(matte, glass, x0, x1, 1, 22), 2) << "the strip must look the same with glass on and off";
}

TEST(TitlebarDraw, RedrawingWithTheCachesWarmIsMuchCheaperThanDrawingFromScratch) {
  TdImage img(800, 32);
  td::TitlebarPaint p = td_paint(800, true);
  auto time_us = [&](bool cold) {
    td_clear();
    double total = 0;
    for (int i = 0; i < 300; ++i) {
      p.hover_id = (i % 5) - 1;
      if (cold) td_clear();
      cairo_t* cr = cairo_create(img.surf);
      cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
      cairo_paint(cr);
      cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
      const double t0 = td_now_us();
      td::draw_titlebar(cr, 800, 32, p, td::Palette{});
      total += td_now_us() - t0;
      cairo_destroy(cr);
    }
    return total / 300;
  };
  const double cold = time_us(true), warm = time_us(false);
  EXPECT_LT(warm, cold * 0.6) << "warm " << warm << " us, cold " << cold << " us: the caches should save well over a third";
}

TEST(TitlebarDraw, TheCompositorUsesTheSharedDrawingAndTheSettingsRowNamesAero) {
  const std::string tb = td_read("src/compositor/titlebar.cpp");
  EXPECT_NE(tb.find("kit::draw_titlebar("), std::string::npos);
  EXPECT_EQ(tb.find("cairo_pattern_create_linear"), std::string::npos) << "the glass gradients live in fleetkit now, where they are cached";
  const std::string settings = td_read("apps/settings/main.cpp");
  EXPECT_NE(settings.find("ui.row(\"Glass effects (Windows Aero)\")"), std::string::npos) << "the Settings row must say Windows Aero in brackets";
  EXPECT_NE(settings.find("Translucent frames and menus"), std::string::npos);
}

TEST(TitlebarDraw, TheDocsRecordTheGlassPerformanceWork) {
  const std::string readme = td_read("README.md");
  const size_t glass = readme.find("## Glass effects");
  ASSERT_NE(glass, std::string::npos);
  const std::string glass_section = readme.substr(glass, readme.find("\n## ", glass + 5) - glass);
  EXPECT_NE(glass_section.find("Speed."), std::string::npos) << "the Glass effects section of the README must say how fast it is";
  EXPECT_NE(glass_section.find("Windows Aero"), std::string::npos);
  EXPECT_NE(glass_section.find("51 us"), std::string::npos);
  EXPECT_NE(glass_section.find("0.28%"), std::string::npos);
  const std::string opt = td_read("docs/OPTIMIZATIONS.md");
  EXPECT_NE(opt.find("## Glass (Windows Aero) performance at a glance"), std::string::npos);
  for (const char* item : {"tile cache", "load_backdrop()", "caption strip", "island_resize_if_needed()", "card-sized surface", "342 us"})
    EXPECT_NE(opt.find(item), std::string::npos) << "OPTIMIZATIONS.md should list: " << item;
  EXPECT_NE(readme.find("docs/OPTIMIZATIONS.md#glass-windows-aero-performance-at-a-glance"), std::string::npos) << "the README links to the table";
  EXPECT_NE(td_read("CHANGELOG.md").find("Windows Aero (glass) performance"), std::string::npos);
}
