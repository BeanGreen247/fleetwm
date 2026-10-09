// Pointer motion in the immediate-mode toolkit: a move repaints only when it can change the drawing.
#include <gtest/gtest.h>

#include <cairo.h>

#include "ui.hpp"

using fleetwm::kit::Palette;
using fleetwm::kit::Ui;

namespace {
struct Frame {
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 400, 300);
  cairo_t* cr = cairo_create(surf);
  ~Frame() {
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
  }
};

// Two buttons side by side at the top; returns after one frame.
void draw_buttons(Ui& ui, Frame& f) {
  ui.begin(f.cr, 400, 300);
  ui.button("One");
  ui.same_line();
  ui.button("Two");
  ui.end();
}
}  // namespace

TEST(UiMotion, MovesInsideOneButtonDoNotRepaint) {
  Ui ui{Palette{}};
  Frame f;
  ui.pointer_motion(200, 200);
  draw_buttons(ui, f);
  EXPECT_TRUE(ui.pointer_motion(30, 30));  // onto the first button: crossed its edge
  draw_buttons(ui, f);
  for (int i = 0; i < 20; ++i) EXPECT_FALSE(ui.pointer_motion(30 + i % 5, 30 + i % 3));
}

TEST(UiMotion, CrossingBetweenButtonsAndOutRepaints) {
  Ui ui{Palette{}};
  Frame f;
  draw_buttons(ui, f);
  ASSERT_TRUE(ui.pointer_motion(30, 30));
  draw_buttons(ui, f);
  EXPECT_TRUE(ui.pointer_motion(200, 200));  // off every button
  draw_buttons(ui, f);
  EXPECT_FALSE(ui.pointer_motion(250, 220));  // still on empty space
}

TEST(UiMotion, HeldButtonAlwaysRepaints) {
  Ui ui{Palette{}};
  Frame f;
  draw_buttons(ui, f);
  ui.pointer_button(200, 200, 0x110, true);
  EXPECT_TRUE(ui.pointer_motion(201, 200));
  EXPECT_TRUE(ui.pointer_motion(202, 200));
}

TEST(UiMotion, CanvasSeesEveryMove) {
  Ui ui{Palette{}};
  Frame f;
  ui.begin(f.cr, 400, 300);
  fleetwm::kit::UiRect r;
  ui.canvas(100, &r);
  ui.end();
  EXPECT_TRUE(ui.pointer_motion(50, 50));
  EXPECT_TRUE(ui.pointer_motion(51, 50));
}
