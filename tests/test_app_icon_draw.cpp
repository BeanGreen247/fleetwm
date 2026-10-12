#include "app_icon.hpp"

#include <cairo.h>
#include <gtest/gtest.h>

using namespace fleetwm::kit;

namespace {
cairo_surface_t* draw_icon_solid(int size) {
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
  cairo_t* c = cairo_create(s);
  cairo_set_source_rgba(c, 1, 0, 0, 1);
  cairo_paint(c);
  cairo_destroy(c);
  return s;
}
uint32_t draw_icon_pixel(cairo_surface_t* s, int x, int y) {
  cairo_surface_flush(s);
  const auto* row = reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(s) + y * cairo_image_surface_get_stride(s));
  return row[x];
}
}  // namespace

TEST(DrawAppIcon, FillsTheBoxAndNothingOutsideIt) {
  cairo_surface_t* icon = draw_icon_solid(48);
  cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 40, 40);
  cairo_t* cr = cairo_create(out);
  draw_app_icon(cr, icon, 5, 5, 22);
  EXPECT_EQ(draw_icon_pixel(out, 15, 15), 0xffff0000u);
  EXPECT_EQ(draw_icon_pixel(out, 2, 2), 0u);
  EXPECT_EQ(draw_icon_pixel(out, 30, 30), 0u);
  cairo_destroy(cr);
  cairo_surface_destroy(out);
  cairo_surface_destroy(icon);
}

TEST(DrawAppIcon, SecondDrawGivesTheSamePixelsAndAlphaScalesThem) {
  cairo_surface_t* icon = draw_icon_solid(48);
  cairo_surface_t* a = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 30, 30);
  cairo_surface_t* b = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 30, 30);
  cairo_t* ca = cairo_create(a);
  cairo_t* cb = cairo_create(b);
  draw_app_icon(ca, icon, 4, 4, 22);
  draw_app_icon(cb, icon, 4, 4, 22);
  EXPECT_EQ(draw_icon_pixel(a, 10, 10), draw_icon_pixel(b, 10, 10));
  cairo_surface_t* half = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 30, 30);
  cairo_t* ch = cairo_create(half);
  draw_app_icon(ch, icon, 4, 4, 22, 0.5);
  const uint32_t alpha = draw_icon_pixel(half, 10, 10) >> 24;
  EXPECT_GE(alpha, 126u);
  EXPECT_LE(alpha, 129u);
  for (cairo_t* c : {ca, cb, ch}) cairo_destroy(c);
  for (cairo_surface_t* s : {a, b, half, icon}) cairo_surface_destroy(s);
}

TEST(DrawAppIcon, FollowsTheDeviceScaleForSharpHiDpiIcons) {
  cairo_surface_t* icon = draw_icon_solid(48);
  cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 80, 80);
  cairo_t* cr = cairo_create(out);
  cairo_scale(cr, 2, 2);  // a 2x output: the 22 logical px box is 44 device px
  draw_app_icon(cr, icon, 5, 5, 22);
  EXPECT_EQ(draw_icon_pixel(out, 30, 30), 0xffff0000u);
  EXPECT_EQ(draw_icon_pixel(out, 8, 8), 0u);
  EXPECT_EQ(draw_icon_pixel(out, 60, 60), 0u);
  cairo_destroy(cr);
  cairo_surface_destroy(out);
  cairo_surface_destroy(icon);
}

TEST(DrawAppIcon, IgnoresAnEmptyBox) {
  cairo_surface_t* icon = draw_icon_solid(48);
  cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 10, 10);
  cairo_t* cr = cairo_create(out);
  draw_app_icon(cr, icon, 0, 0, 0);
  EXPECT_EQ(draw_icon_pixel(out, 5, 5), 0u);
  cairo_destroy(cr);
  cairo_surface_destroy(out);
  cairo_surface_destroy(icon);
}
