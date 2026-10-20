// Text that Inter has no glyph for (braille, dingbats, box drawing: the art in a terminal's window title) is drawn with a font that has
// it instead of an empty box. Each character is a different picture, measured and drawn with the same advance, and Latin text is unchanged.
#include <gtest/gtest.h>

#include <cairo.h>

#include <cstring>
#include <string>
#include <vector>

#include "fleetkit.hpp"

namespace {

namespace kit = fleetwm::kit;

std::vector<unsigned char> render(const std::string& text, double* advance = nullptr) {
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 120, 30);
  cairo_t* cr = cairo_create(surf);
  const double adv = kit::draw_text(cr, text, 4, 22, 16, kit::Color{1, 1, 1, 1});
  if (advance) *advance = adv;
  cairo_destroy(cr);
  cairo_surface_flush(surf);
  const unsigned char* d = cairo_image_surface_get_data(surf);
  std::vector<unsigned char> out(d, d + cairo_image_surface_get_stride(surf) * 30);
  cairo_surface_destroy(surf);
  return out;
}

double measured(const std::string& text) {
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 10, 10);
  cairo_t* cr = cairo_create(surf);
  const double w = kit::measure_text(cr, text, 16).width;
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return w;
}

TEST(TextFallback, DifferentSymbolsAreDifferentPictures) {
  // Without a fallback every one of these is the same empty box.
  EXPECT_NE(render("\xE2\xA0\x81"), render("\xE2\xA0\xBF"));  // braille dots 1 and all 8 (U+2801, U+283F)
  EXPECT_NE(render("\xE2\x9C\xB3"), render("\xE2\x9D\xAF"));  // U+2733 and U+276F
}

TEST(TextFallback, MeasureAgreesWithDraw) {
  for (const char* t : {"\xE2\x9C\xB3 title", "a\xE2\xA0\xBF" "b", "\xE2\x94\x8C\xE2\x94\x80\xE2\x94\x90"}) {
    double adv = 0;
    render(t, &adv);
    EXPECT_NEAR(measured(t), adv, 0.01) << t;
    EXPECT_GT(adv, 4) << t;
  }
}

TEST(TextFallback, LatinTextTakesTheOldPath) {
  double adv = 0;
  render("Window title", &adv);
  EXPECT_NEAR(measured("Window title"), adv, 0.01);
}

TEST(TextFallback, BadUtf8DoesNotCrash) {
  EXPECT_GE(measured(std::string("a\xE2\x9C", 3)), 0);
  EXPECT_GE(measured("\xFF\xFE"), 0);
}

}  // namespace
