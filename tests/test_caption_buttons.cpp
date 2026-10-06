// The window caption buttons: pin, minimize, maximize/restore and close as one joined, glossy strip in the
// style of Windows 7. These tests render the strip with cairo and look at the pixels: which button is red,
// that a hovered one is lit and glows (blue; orange behind close), that the strip is rounded only at the
// bottom, that the glyphs are white, and that the compositor uses it.
#include <gtest/gtest.h>

#include <cairo.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>

#include "caption_buttons.hpp"
#include "window_geometry.hpp"

namespace {

using fleetwm::kit::CaptionButton;
using fleetwm::kit::CaptionState;
namespace cb_geom = fleetwm::geom;

constexpr int kW = 220, kH = 60;
constexpr double kX0 = 40, kY0 = 6, kBtnW = 38, kBtnH = 24;

struct CbImage {
  cairo_surface_t* surf = nullptr;
  const unsigned char* data = nullptr;
  int stride = 0;
  ~CbImage() {
    if (surf) cairo_surface_destroy(surf);
  }
  // un-premultiplied colour and alpha of a pixel
  void pixel(int x, int y, double* r, double* g, double* b, double* a) const {
    const unsigned char* p = data + y * stride + x * 4;
    const double alpha = p[3] / 255.0;
    *a = alpha;
    *b = alpha > 0 ? p[0] / 255.0 / alpha : 0;
    *g = alpha > 0 ? p[1] / 255.0 / alpha : 0;
    *r = alpha > 0 ? p[2] / 255.0 / alpha : 0;
  }
  double alpha(int x, int y) const {
    double r, g, b, a;
    pixel(x, y, &r, &g, &b, &a);
    return a;
  }
};

std::vector<CaptionButton> cb_strip() {
  std::vector<CaptionButton> v;
  double x = kX0;
  for (int id : {cb_geom::kBtnPin, cb_geom::kBtnMinimize, cb_geom::kBtnMaximize, cb_geom::kBtnClose}) {
    const double w = id == cb_geom::kBtnClose ? std::round(kBtnW * cb_geom::kStripCloseScale) : kBtnW;
    v.push_back({id, x, kY0, w, kBtnH});
    x += w;
  }
  return v;
}

CbImage cb_render(const CaptionState& st) {
  CbImage img;
  img.surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kW + 100, kH + 20);
  cairo_t* cr = cairo_create(img.surf);
  const std::vector<CaptionButton> strip = cb_strip();
  // The test surface is bigger than the strip so a glow below or beside it can be seen; nothing is clipped.
  cairo_translate(cr, 30, 4);
  fleetwm::kit::draw_caption_buttons(cr, strip.data(), static_cast<int>(strip.size()), st);
  cairo_destroy(cr);
  cairo_surface_flush(img.surf);
  img.data = cairo_image_surface_get_data(img.surf);
  img.stride = cairo_image_surface_get_stride(img.surf);
  return img;
}

// Average colour of a rectangle of the rendered strip (strip coordinates).
void cb_average(const CbImage& img, double x, double y, double w, double h, double* r, double* g, double* b, double* a) {
  double sr = 0, sg = 0, sb = 0, sa = 0;
  int n = 0;
  for (int py = static_cast<int>(y); py < static_cast<int>(y + h); ++py)
    for (int px = static_cast<int>(x); px < static_cast<int>(x + w); ++px) {
      double pr, pg, pb, pa;
      img.pixel(px + 30, py + 4, &pr, &pg, &pb, &pa);
      sr += pr * pa;
      sg += pg * pa;
      sb += pb * pa;
      sa += pa;
      ++n;
    }
  *r = sa > 0 ? sr / sa : 0;
  *g = sa > 0 ? sg / sa : 0;
  *b = sa > 0 ? sb / sa : 0;
  *a = n > 0 ? sa / n : 0;
}

CaptionButton cb_button(int id) {
  for (const CaptionButton& b : cb_strip())
    if (b.id == id) return b;
  return {};
}

// Colour of the glass of one button, sampled away from its glyph (left part of the lower half).
void cb_glass(const CbImage& img, int id, double* r, double* g, double* b, double* a) {
  const CaptionButton btn = cb_button(id);
  cb_average(img, btn.x + 3, btn.y + btn.h * 0.58, btn.w * 0.22, btn.h * 0.30, r, g, b, a);
}

double cb_luma(double r, double g, double b) { return 0.2126 * r + 0.7152 * g + 0.0722 * b; }
double cb_dist(double r1, double g1, double b1, double r2, double g2, double b2) {
  return std::sqrt((r1 - r2) * (r1 - r2) + (g1 - g2) * (g1 - g2) + (b1 - b2) * (b1 - b2));
}

// The theme colours of a light theme (dark ones are the defaults of CaptionColors).
fleetwm::kit::CaptionColors cb_light_colors() {
  fleetwm::kit::CaptionColors c;
  c.bg = {0.90, 0.91, 0.94, 1};
  c.fg = {0.14, 0.15, 0.20, 1};
  c.accent = {0.17, 0.42, 0.85, 1};
  return c;
}

std::string cb_read(const char* rel) {
  std::ifstream in(std::string(FLEETWM_SOURCE_DIR) + "/" + rel);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

TEST(CaptionButtons, IdsMatchTheGeometryModule) {
  // caption_buttons.cpp spells the ids out (it does not include the geometry header); they must not drift.
  EXPECT_EQ(cb_geom::kBtnMaximize, 0);
  EXPECT_EQ(cb_geom::kBtnClose, 1);
  EXPECT_EQ(cb_geom::kBtnMinimize, 2);
  EXPECT_EQ(cb_geom::kBtnPin, 3);
}

TEST(CaptionButtons, CloseIsRedAndTheOthersFollowTheThemeColour) {
  for (int light = 0; light < 2; ++light) {
    CaptionState st;
    if (light) st.colors = cb_light_colors();
    const CbImage img = cb_render(st);
    double r, g, b, a;
    cb_glass(img, cb_geom::kBtnClose, &r, &g, &b, &a);
    EXPECT_GT(r, g + 0.2) << "close is red in every theme (light " << light << ")";
    EXPECT_GT(r, b + 0.2);
    const double bg_luma = cb_luma(st.colors.bg.r, st.colors.bg.g, st.colors.bg.b);
    for (int id : {cb_geom::kBtnPin, cb_geom::kBtnMinimize, cb_geom::kBtnMaximize}) {
      cb_glass(img, id, &r, &g, &b, &a);
      const double l = cb_luma(r, g, b);
      EXPECT_LT(std::fabs(l - bg_luma), 0.40) << "button " << id << " must sit close to the titlebar's own brightness, light=" << light;
      if (light) EXPECT_GT(l, 0.6) << "on a light theme the buttons are light";
      else EXPECT_LT(l, 0.45) << "on a dark theme the buttons are dark glass, not bright slabs";
    }
  }
}

TEST(CaptionButtons, GlassIsTwoToneLighterAboveTheMiddleThanBelow) {
  const CbImage img = cb_render({});
  for (int id : {cb_geom::kBtnMinimize, cb_geom::kBtnClose}) {
    const CaptionButton btn = cb_button(id);
    double r1, g1, b1, a1, r2, g2, b2, a2;
    cb_average(img, btn.x + 3, btn.y + 2, btn.w * 0.2, btn.h * 0.25, &r1, &g1, &b1, &a1);
    cb_average(img, btn.x + 3, btn.y + btn.h * 0.6, btn.w * 0.2, btn.h * 0.25, &r2, &g2, &b2, &a2);
    EXPECT_GT(r1 + g1 + b1, r2 + g2 + b2 + 0.15) << "button " << id << ": the upper half is the shine";
  }
}

TEST(CaptionButtons, TheStripIsOneSolidPieceWithNoGapsBetweenButtons) {
  const CbImage img = cb_render({});
  const std::vector<CaptionButton> strip = cb_strip();
  const double x1 = strip.back().x + strip.back().w;
  for (int x = static_cast<int>(kX0) + 3; x < static_cast<int>(x1) - 3; ++x)
    EXPECT_GT(img.alpha(x + 30, static_cast<int>(kY0 + kBtnH / 2) + 4), 0.97) << "gap at x=" << x;
}

TEST(CaptionButtons, TopCornersAreSquareAndBottomCornersRounded) {
  const CbImage img = cb_render({});
  const std::vector<CaptionButton> strip = cb_strip();
  const int left = static_cast<int>(kX0), right = static_cast<int>(strip.back().x + strip.back().w) - 1;
  const int top = static_cast<int>(kY0), bottom = static_cast<int>(kY0 + kBtnH) - 1;
  EXPECT_GT(img.alpha(left + 30 + 1, top + 4 + 1), 0.9) << "top left lies on the window's edge";
  EXPECT_GT(img.alpha(right + 30 - 1, top + 4 + 1), 0.9) << "top right";
  EXPECT_LT(img.alpha(right + 30, bottom + 4), 0.5) << "bottom right is rounded";
  EXPECT_LT(img.alpha(left + 30, bottom + 4), 0.5) << "bottom left is rounded";
  EXPECT_GT(img.alpha(right + 30 - 8, bottom + 4), 0.9) << "the bottom edge itself is solid";
}

TEST(CaptionButtons, GlyphsAreWhiteWithADarkOutline) {
  const CbImage img = cb_render({});
  const CaptionButton close = cb_button(cb_geom::kBtnClose);
  const double cx = close.x + close.w / 2, cy = close.y + close.h / 2;
  double r, g, b, a;
  img.pixel(static_cast<int>(cx) + 30, static_cast<int>(cy) + 4, &r, &g, &b, &a);
  EXPECT_GT(std::min({r, g, b}), 0.9) << "the middle of the X is white";
  // somewhere around the X there must be dark outline pixels, and the X reaches into the corners of its box
  int dark = 0, white = 0;
  for (int y = static_cast<int>(cy) - 7; y <= static_cast<int>(cy) + 7; ++y)
    for (int x = static_cast<int>(cx) - 8; x <= static_cast<int>(cx) + 8; ++x) {
      img.pixel(x + 30, y + 4, &r, &g, &b, &a);
      if (a > 0.9 && r + g + b < 0.9) ++dark;
      if (a > 0.9 && std::min({r, g, b}) > 0.9) ++white;
    }
  EXPECT_GT(dark, 12) << "the glyph has a dark outline";
  EXPECT_GT(white, 20);
  // the arms of the X: a diagonal pixel is white, the pixel beside the centre on the axis is not
  img.pixel(static_cast<int>(cx + 3) + 30, static_cast<int>(cy - 3) + 4, &r, &g, &b, &a);
  EXPECT_GT(std::min({r, g, b}), 0.85) << "the arm of the X towards the upper right";
  img.pixel(static_cast<int>(cx + 5) + 30, static_cast<int>(cy) + 4, &r, &g, &b, &a);
  EXPECT_LT(std::min({r, g, b}), 0.85) << "between the arms it is red glass";
}

TEST(CaptionButtons, HoveredMinimizeAndMaximizeTakeTheAccentColourAndGlow) {
  CaptionState hover;
  hover.hover_id = cb_geom::kBtnMinimize;
  const CbImage normal = cb_render({}), lit = cb_render(hover);
  double r0, g0, b0, a0, r1, g1, b1, a1;
  cb_glass(normal, cb_geom::kBtnMinimize, &r0, &g0, &b0, &a0);
  cb_glass(lit, cb_geom::kBtnMinimize, &r1, &g1, &b1, &a1);
  const fleetwm::kit::Color acc = CaptionState{}.colors.accent;
  EXPECT_GT(cb_luma(r1, g1, b1), cb_luma(r0, g0, b0) + 0.12) << "a hovered button is lit";
  EXPECT_LT(cb_dist(r1, g1, b1, acc.r, acc.g, acc.b), cb_dist(r0, g0, b0, acc.r, acc.g, acc.b) - 0.1) << "and moves towards the accent colour";
  // The glow: just below the strip it is transparent at rest and the accent colour on hover.
  const CaptionButton btn = cb_button(cb_geom::kBtnMinimize);
  double rg, gg, bg, ag, rn, gn, bn, an;
  cb_average(lit, btn.x + btn.w / 2 - 4, kY0 + kBtnH + 1, 8, 5, &rg, &gg, &bg, &ag);
  cb_average(normal, btn.x + btn.w / 2 - 4, kY0 + kBtnH + 1, 8, 5, &rn, &gn, &bn, &an);
  EXPECT_LT(an, 0.02) << "nothing below the strip when idle";
  EXPECT_GT(ag, 0.15) << "a glow below the hovered button";
  EXPECT_GT(bg, rg + 0.2) << "the glow is blue for a blue accent";
}

TEST(CaptionButtons, TheAccentColourOfTheThemeDrivesTheHoverAndTheGlow) {
  CaptionState green;
  green.colors.accent = {0.30, 0.78, 0.45, 1};
  green.hover_id = cb_geom::kBtnMaximize;
  const CbImage img = cb_render(green);
  double r, g, b, a;
  cb_glass(img, cb_geom::kBtnMaximize, &r, &g, &b, &a);
  EXPECT_GT(g, r + 0.15) << "a green accent gives a green hovered button";
  EXPECT_GT(g, b + 0.05);
  const CaptionButton btn = cb_button(cb_geom::kBtnMaximize);
  double rg, gg, bg, ag;
  cb_average(img, btn.x + btn.w / 2 - 4, kY0 + kBtnH + 1, 8, 5, &rg, &gg, &bg, &ag);
  EXPECT_GT(gg, rg + 0.15) << "and a green glow";
  EXPECT_GT(gg, bg);
}

TEST(CaptionButtons, GlassModeMakesTheButtonsTranslucentAndMatteKeepsThemSolid) {
  CaptionState glass;
  glass.glass = true;
  const CbImage matte = cb_render({}), see_through = cb_render(glass);
  double r, g, b, a_matte, a_glass, a_close_matte, a_close_glass;
  cb_glass(matte, cb_geom::kBtnMinimize, &r, &g, &b, &a_matte);
  cb_glass(see_through, cb_geom::kBtnMinimize, &r, &g, &b, &a_glass);
  cb_glass(matte, cb_geom::kBtnClose, &r, &g, &b, &a_close_matte);
  cb_glass(see_through, cb_geom::kBtnClose, &r, &g, &b, &a_close_glass);
  EXPECT_GT(a_matte, 0.98);
  EXPECT_LT(a_glass, 0.9) << "glass buttons let the wallpaper tint through like the bar behind them";
  EXPECT_GT(a_glass, 0.6) << "but stay readable";
  EXPECT_GT(a_close_matte, 0.98);
  EXPECT_GT(a_close_glass, a_glass) << "close is the most solid button";
  // the strip is still one piece in glass mode: no gaps between the buttons
  const std::vector<CaptionButton> strip = cb_strip();
  const double x1 = strip.back().x + strip.back().w;
  for (int x = static_cast<int>(kX0) + 3; x < static_cast<int>(x1) - 3; ++x)
    EXPECT_GT(see_through.alpha(x + 30, static_cast<int>(kY0 + kBtnH / 2) + 4), 0.55) << "gap at x=" << x;
}

TEST(CaptionButtons, HoveredCloseTurnsOrangeAndGlowsOrange) {
  CaptionState hover;
  hover.hover_id = cb_geom::kBtnClose;
  const CbImage normal = cb_render({}), lit = cb_render(hover);
  double r0, g0, b0, a0, r1, g1, b1, a1;
  cb_glass(normal, cb_geom::kBtnClose, &r0, &g0, &b0, &a0);
  cb_glass(lit, cb_geom::kBtnClose, &r1, &g1, &b1, &a1);
  EXPECT_GT(g1, g0 + 0.05) << "brighter, more orange";
  EXPECT_GT(r1, 0.75);
  EXPECT_GT(r1, b1 + 0.3);
  const CaptionButton btn = cb_button(cb_geom::kBtnClose);
  double rg, gg, bg, ag;
  cb_average(lit, btn.x + btn.w / 2 - 4, kY0 + kBtnH + 1, 8, 5, &rg, &gg, &bg, &ag);
  EXPECT_GT(ag, 0.15) << "an orange glow below close";
  EXPECT_GT(rg, bg + 0.4) << "the glow is orange";
}

TEST(CaptionButtons, PressedIsDarkerThanHovered) {
  CaptionState hover, pressed;
  hover.hover_id = pressed.hover_id = pressed.pressed_id = cb_geom::kBtnClose;
  double rh, gh, bh, ah, rp, gp, bp, ap;
  cb_glass(cb_render(hover), cb_geom::kBtnClose, &rh, &gh, &bh, &ah);
  cb_glass(cb_render(pressed), cb_geom::kBtnClose, &rp, &gp, &bp, &ap);
  EXPECT_LT(rp + gp + bp, rh + gh + bh - 0.3);
}

TEST(CaptionButtons, PinnedLooksPushedInAndRestoreIsNotTheMaximizeGlyph) {
  CaptionState pinned, maximized;
  pinned.pinned = true;
  maximized.maximized = true;
  const CbImage rest = cb_render({}), pin = cb_render(pinned), max = cb_render(maximized);
  double r0, g0, b0, a0, r1, g1, b1, a1;
  cb_glass(rest, cb_geom::kBtnPin, &r0, &g0, &b0, &a0);
  cb_glass(pin, cb_geom::kBtnPin, &r1, &g1, &b1, &a1);
  EXPECT_GT(cb_dist(r0, g0, b0, r1, g1, b1), 0.12) << "a pinned window's pin button is toggled on (pushed in, in the accent colour)";
  // restore draws two windows: more white ink than the single maximize square
  auto ink = [](const CbImage& img, int id) {
    const CaptionButton btn = cb_button(id);
    int white = 0;
    for (int y = static_cast<int>(btn.y); y < static_cast<int>(btn.y + btn.h); ++y)
      for (int x = static_cast<int>(btn.x); x < static_cast<int>(btn.x + btn.w); ++x) {
        double r, g, b, a;
        img.pixel(x + 30, y + 4, &r, &g, &b, &a);
        if (a > 0.9 && std::min({r, g, b}) > 0.72) ++white;
      }
    return white;
  };
  EXPECT_NE(ink(rest, cb_geom::kBtnMaximize), ink(max, cb_geom::kBtnMaximize)) << "restore looks different from maximize";
  EXPECT_GT(ink(rest, cb_geom::kBtnMaximize), 8);
  EXPECT_GT(ink(max, cb_geom::kBtnMaximize), 8);
}

TEST(CaptionButtons, AnInactiveWindowsButtonsFade) {
  CaptionState inactive;
  inactive.focused = false;
  double r, g, b, active_alpha, faded_alpha;
  cb_glass(cb_render({}), cb_geom::kBtnMinimize, &r, &g, &b, &active_alpha);
  cb_glass(cb_render(inactive), cb_geom::kBtnMinimize, &r, &g, &b, &faded_alpha);
  EXPECT_GT(active_alpha, 0.95);
  EXPECT_LT(faded_alpha, active_alpha - 0.2);
}

TEST(CaptionButtons, NothingIsDrawnOutsideTheStripWhenNoButtonIsHovered) {
  const CbImage img = cb_render({});
  const std::vector<CaptionButton> strip = cb_strip();
  const int x1 = static_cast<int>(strip.back().x + strip.back().w);
  for (int y = 0; y < kH + 20; ++y)
    for (int x = 0; x < kW + 100; ++x) {
      const bool inside = x >= 30 + static_cast<int>(kX0) - 1 && x <= 30 + x1 && y >= 4 + static_cast<int>(kY0) - 1 && y <= 4 + static_cast<int>(kY0 + kBtnH) + 1;
      if (!inside) ASSERT_LT(img.alpha(x, y), 0.01) << "stray pixel at " << x << "," << y;
    }
}

TEST(CaptionButtons, AnEmptyStripDrawsNothingAndDoesNotCrash) {
  CbImage img;
  img.surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 10, 10);
  cairo_t* cr = cairo_create(img.surf);
  fleetwm::kit::draw_caption_buttons(cr, nullptr, 0, {});
  cairo_destroy(cr);
  cairo_surface_flush(img.surf);
  img.data = cairo_image_surface_get_data(img.surf);
  img.stride = cairo_image_surface_get_stride(img.surf);
  EXPECT_EQ(img.alpha(5, 5), 0.0);
}

TEST(CaptionButtons, TheCompositorDrawsItsTitlebarButtonsWithTheStrip) {
  const std::string titlebar = cb_read("src/compositor/titlebar.cpp");
  EXPECT_NE(titlebar.find("m.strip = true"), std::string::npos) << "the layout must be the joined strip";
  EXPECT_NE(titlebar.find("kit::draw_titlebar("), std::string::npos);
  EXPECT_NE(titlebar.find("paint.hover_id = st.hover_button"), std::string::npos);
  const std::string draw = cb_read("src/fleetkit/titlebar_draw.cpp");
  EXPECT_NE(draw.find("caption.hover_id = p.hover_id"), std::string::npos);
  EXPECT_NE(draw.find("draw_caption_buttons("), std::string::npos);
  EXPECT_EQ(draw.find("cairo_arc(cr, cx, cy, rr, 0, 2 * M_PI)"), std::string::npos) << "the old round orbs are gone";
}

// ---- the same strip in glass and in matte mode (Desktop layout) ------------------------------------------

TEST(CaptionButtons, TheStripIsDrawnTheSameWayWhetherGlassIsOnOrOff) {
  const std::string titlebar = cb_read("src/fleetkit/titlebar_draw.cpp");
  const size_t draw = titlebar.find("draw_strip_cached(cr, strip, p.layout.count");
  ASSERT_NE(draw, std::string::npos);
  // The call must not sit inside an `if (st.glass)` / `else` branch: find the last `if (st.glass)` before
  // the call and check that its block was closed before the call.
  const size_t last_glass_if = titlebar.rfind("if (p.glass)", draw);
  ASSERT_NE(last_glass_if, std::string::npos) << "the background still differs between glass and matte";
  int depth = 0;
  bool opened = false;
  size_t i = titlebar.find('{', last_glass_if);
  for (; i < draw; ++i) {
    if (titlebar[i] == '{') {
      ++depth;
      opened = true;
    } else if (titlebar[i] == '}') {
      --depth;
    }
    if (opened && depth == 0) break;
  }
  // Skip an `} else {` that continues the same statement.
  const size_t rest = titlebar.find_first_not_of(" \n", i + 1);
  const bool else_follows = rest != std::string::npos && titlebar.compare(rest, 4, "else") == 0;
  size_t end_of_statement = i;
  if (else_follows) {
    int d = 0;
    for (size_t j = titlebar.find('{', rest); j < draw; ++j) {
      if (titlebar[j] == '{') ++d;
      if (titlebar[j] == '}' && --d == 0) {
        end_of_statement = j;
        break;
      }
    }
  }
  EXPECT_LT(end_of_statement, draw) << "the caption strip is drawn inside the glass/matte branch: it must be drawn for both";
  EXPECT_EQ(titlebar.find("p.glass", draw), std::string::npos) << "nothing after the call may depend on glass either";
  // and the buttons only exist in the Desktop layout, which is where the compositor draws titlebars at all
  const std::string view = cb_read("src/compositor/view.cpp");
  EXPECT_NE(view.find("if (!desktop_mode() || fullscreen || !is_window())"), std::string::npos);
}

TEST(CaptionButtons, TheStateCarriesTheThemeColoursAndTheGlassFlag) {
  const std::string header = cb_read("src/fleetkit/caption_buttons.hpp");
  const size_t at = header.find("struct CaptionState");
  ASSERT_NE(at, std::string::npos);
  const std::string state = header.substr(at, header.find("};", at) - at);
  EXPECT_NE(state.find("CaptionColors colors"), std::string::npos) << "the strip is made from the theme, not from fixed blues";
  EXPECT_NE(state.find("bool glass"), std::string::npos) << "and it knows whether the bar is glass";
  const std::string draw = cb_read("src/fleetkit/titlebar_draw.cpp");
  EXPECT_NE(draw.find("caption.colors = {bg, fg, pal.accent}"), std::string::npos);
  EXPECT_NE(draw.find("caption.glass = p.glass"), std::string::npos);
  const CbImage a = cb_render({}), b = cb_render({});
  for (int y = 0; y < kH + 20; y += 3)
    for (int x = 0; x < kW + 100; x += 3) ASSERT_EQ(a.alpha(x, y), b.alpha(x, y));
}
