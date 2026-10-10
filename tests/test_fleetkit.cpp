// Unit tests for the pure (no Wayland) parts of src/fleetkit: desktop-entry Exec
// expansion, colour parsing, icon-less helpers.
#include <gtest/gtest.h>

#include "desktop_entry.hpp"
#include "fleetkit.hpp"

using fleetwm::kit::DesktopEntry;
using fleetwm::kit::exec_argv;
using fleetwm::kit::exec_basename;
using fleetwm::kit::parse_color;

namespace {
DesktopEntry entry(const std::string& exec) {
  DesktopEntry e;
  e.name = "Demo App";
  e.file_path = "/usr/share/applications/demo.desktop";
  e.icon = "demo-icon";
  e.exec = exec;
  return e;
}
}  // namespace

TEST(DesktopEntryExec, DropsFileFieldCodes) {
  const auto argv = exec_argv(entry("firefox %u"));
  ASSERT_EQ(argv.size(), 1u);
  EXPECT_EQ(argv[0], "firefox");
}

TEST(DesktopEntryExec, KeepsPlainArguments) {
  const auto argv = exec_argv(entry("env FOO=1 /usr/bin/app --flag %F"));
  ASSERT_EQ(argv.size(), 4u);
  EXPECT_EQ(argv[0], "env");
  EXPECT_EQ(argv[3], "--flag");
}

TEST(DesktopEntryExec, QuotedArgumentWithSpaces) {
  const auto argv = exec_argv(entry("sh -c \"echo hello world\""));
  ASSERT_EQ(argv.size(), 3u);
  EXPECT_EQ(argv[2], "echo hello world");
}

TEST(DesktopEntryExec, EscapedQuoteInsideQuotes) {
  const auto argv = exec_argv(entry("app \"say \\\"hi\\\"\""));
  ASSERT_EQ(argv.size(), 2u);
  EXPECT_EQ(argv[1], "say \"hi\"");
}

TEST(DesktopEntryExec, IconNameAndKeyCodes) {
  const auto argv = exec_argv(entry("app %i %c %k 100%%"));
  ASSERT_EQ(argv.size(), 6u);
  EXPECT_EQ(argv[1], "--icon");
  EXPECT_EQ(argv[2], "demo-icon");
  EXPECT_EQ(argv[3], "Demo App");
  EXPECT_EQ(argv[4], "/usr/share/applications/demo.desktop");
  EXPECT_EQ(argv[5], "100%");
}

TEST(DesktopEntryExec, BasenameOfAbsolutePath) {
  EXPECT_EQ(exec_basename(entry("/usr/bin/foot --server")), "foot");
  EXPECT_EQ(exec_basename(entry("")), "");
}

TEST(DesktopEntryExec, CarriesStartupWindowClassForTaskbarMatching) {
  DesktopEntry e = entry("fleetwm-fm");
  e.startup_wm_class = "dev.fleetwm.FileManager";
  EXPECT_EQ(e.startup_wm_class, "dev.fleetwm.FileManager");
}

TEST(FleetkitColor, ParsesRgbAndRgba) {
  const auto c = parse_color("#ff8000");
  EXPECT_DOUBLE_EQ(c.r, 1.0);
  EXPECT_NEAR(c.g, 128 / 255.0, 1e-9);
  EXPECT_DOUBLE_EQ(c.b, 0.0);
  EXPECT_DOUBLE_EQ(c.a, 1.0);
  const auto d = parse_color("#00000080");
  EXPECT_NEAR(d.a, 128 / 255.0, 1e-9);
}

TEST(FleetkitColor, MalformedFallsBack) {
  fleetwm::kit::Color fb{0.1, 0.2, 0.3, 1.0};
  EXPECT_DOUBLE_EQ(parse_color("red", fb).r, 0.1);
  EXPECT_DOUBLE_EQ(parse_color("#12345", fb).g, 0.2);
  EXPECT_DOUBLE_EQ(parse_color("#12zz56", fb).b, 0.3);
}

TEST(FleetkitMenu, UsesPaletteAndSharedGeometry) {
  fleetwm::kit::Palette p;
  p.bg_primary = {0.1, 0.2, 0.3, 1.0};
  p.bg_secondary = {0.2, 0.3, 0.4, 1.0};
  p.fg_primary = {0.8, 0.7, 0.6, 1.0};
  p.fg_secondary = {0.5, 0.4, 0.3, 1.0};
  p.accent = {0.9, 0.8, 0.1, 1.0};
  const auto menu = fleetwm::kit::menu_theme(p);
  EXPECT_DOUBLE_EQ(menu.background.r, p.bg_secondary.r);
  EXPECT_DOUBLE_EQ(menu.text.g, p.fg_primary.g);
  EXPECT_DOUBLE_EQ(menu.hover.b, p.accent.b);
  EXPECT_DOUBLE_EQ(menu.hover_text.r, p.bg_primary.r);
  EXPECT_DOUBLE_EQ(menu.radius, 8.0);
  EXPECT_DOUBLE_EQ(menu.item_radius, 5.0);
}

#include "backdrop.hpp"

TEST(Backdrop, BlurKeepsAFlatPictureFlat) {
  std::vector<uint8_t> px(8 * 8 * 4, 120);
  fleetwm::kit::box_blur_rgba(px.data(), 8, 8, 2, 3);
  for (uint8_t v : px) EXPECT_EQ(v, 120);
}

TEST(Backdrop, BlurSpreadsABrightPixelAndKeepsTheTotalCloseToConstant) {
  std::vector<uint8_t> px(9 * 9 * 4, 0);
  px[(4 * 9 + 4) * 4] = 255;
  fleetwm::kit::box_blur_rgba(px.data(), 9, 9, 1, 1);
  EXPECT_LT(px[(4 * 9 + 4) * 4], 255);
  EXPECT_GT(px[(4 * 9 + 5) * 4], 0);
  EXPECT_GT(px[(5 * 9 + 5) * 4], 0);
}

TEST(Backdrop, DownscaleKeepsTheAspectRatioAndNeverEnlarges) {
  std::vector<uint8_t> src(400 * 200 * 4, 77);
  uint8_t* out = nullptr;
  int w = 0, h = 0;
  fleetwm::kit::downscale_rgba(src.data(), 400, 200, 100, &out, &w, &h);
  EXPECT_EQ(w, 100);
  EXPECT_EQ(h, 50);
  EXPECT_EQ(out[0], 77);
  std::free(out);
  fleetwm::kit::downscale_rgba(src.data(), 400, 200, 1000, &out, &w, &h);
  EXPECT_EQ(w, 400);
  std::free(out);
}

namespace {
// A 24x24 backdrop with a diagonal ramp, so a wrong crop or offset shows up in the pixels.
cairo_surface_t* ramp_backdrop() {
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 24, 24);
  unsigned char* d = cairo_image_surface_get_data(s);
  const int stride = cairo_image_surface_get_stride(s);
  for (int y = 0; y < 24; ++y)
    for (int x = 0; x < 24; ++x)
      reinterpret_cast<uint32_t*>(d + y * stride)[x] = (0xFFu << 24) | ((x * 10) << 16) | ((y * 10) << 8) | 0x40;
  cairo_surface_mark_dirty(s);
  return s;
}

int max_channel_diff(cairo_surface_t* a, cairo_surface_t* b, int w, int h, int x0 = 0, int y0 = 0) {
  cairo_surface_flush(a);
  cairo_surface_flush(b);
  const unsigned char *da = cairo_image_surface_get_data(a), *db = cairo_image_surface_get_data(b);
  const int sa = cairo_image_surface_get_stride(a), sb = cairo_image_surface_get_stride(b);
  int worst = 0;
  for (int y = y0; y < y0 + h; ++y)
    for (int x = x0 * 4; x < (x0 + w) * 4; ++x) worst = std::max(worst, std::abs(int(da[y * sa + x]) - int(db[y * sb + x])));
  return worst;
}
}  // namespace

// The cached glass tile must look the same as painting it directly (a tiny sub-pixel offset forces the
// direct path), and a second paint from the cache must be identical to the first.
TEST(GlassCache, TileMatchesDirectPaint) {
  cairo_surface_t* bd = ramp_backdrop();
  fleetwm::kit::GlassStyle st;
  st.radius = 10;
  const int W = 160, H = 60;
  cairo_surface_t* cached = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
  cairo_surface_t* direct = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
  cairo_surface_t* again = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
  cairo_t* c1 = cairo_create(cached);
  cairo_t* c2 = cairo_create(direct);
  cairo_t* c3 = cairo_create(again);
  fleetwm::kit::paint_glass(c1, bd, 800, 600, 0, 0, 0, 0, W, H, st);
  fleetwm::kit::paint_glass(c2, bd, 800, 600, 0, 0, 0.00001, 0, W, H, st);  // not a whole pixel: painted directly
  fleetwm::kit::paint_glass(c3, bd, 800, 600, 0, 0, 0, 0, W, H, st);
  EXPECT_EQ(max_channel_diff(cached, again, W, H), 0);
  EXPECT_LE(max_channel_diff(cached, direct, W, H), 2);  // the direct path, 1e-5 px away
  // Half a pixel away the rim lines move, but the interior must still match the tile.
  cairo_surface_t* frac = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
  cairo_t* c4 = cairo_create(frac);
  fleetwm::kit::paint_glass(c4, bd, 800, 600, 0, 0, 0.5, 0, W - 1, H, st);
  EXPECT_LE(max_channel_diff(cached, frac, W - 24, H - 16, 8, 8), 6);
  for (cairo_t* c : {c1, c2, c3, c4}) cairo_destroy(c);
  for (cairo_surface_t* s : {cached, direct, again, frac, bd}) cairo_surface_destroy(s);
}

TEST(GlassCache, DifferentStyleIsNotServedFromAnotherTile) {
  cairo_surface_t* bd = ramp_backdrop();
  fleetwm::kit::GlassStyle a, b;
  a.tint_alpha = 0.2;
  b.tint_alpha = 0.9;
  const int W = 64, H = 40;
  cairo_surface_t* sa = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
  cairo_surface_t* sb = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
  cairo_t* ca = cairo_create(sa);
  cairo_t* cb = cairo_create(sb);
  fleetwm::kit::paint_glass(ca, bd, 400, 300, 0, 0, 0, 0, W, H, a);
  fleetwm::kit::paint_glass(cb, bd, 400, 300, 0, 0, 0, 0, W, H, b);
  EXPECT_GT(max_channel_diff(sa, sb, W, H), 10);
  cairo_destroy(ca);
  cairo_destroy(cb);
  for (cairo_surface_t* s : {sa, sb, bd}) cairo_surface_destroy(s);
}
