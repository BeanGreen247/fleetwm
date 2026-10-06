#pragma once

// The window caption buttons (pin, minimize, maximize/restore, close) drawn as a joined, glossy strip in
// the style of Windows 7: a light two-tone glass button for pin/minimize/maximize, a red one for close, a
// dark outline with a white inner edge, white glyphs with a dark outline, a blue glow behind a hovered
// button and an orange one behind close. Everything is drawn with cairo paths and gradients (no images).
// The compositor draws its titlebars with it and the unit tests render it to check the result.

#include <cairo.h>

namespace fleetwm::kit {

struct CaptionButton {
  int id = -1;  // geom::TitleButton (window_geometry.hpp): 0 maximize, 1 close, 2 minimize, 3 pin
  double x = 0, y = 0, w = 0, h = 0;
};

struct CaptionState {
  bool focused = true;     // an inactive window's buttons are fainter
  bool maximized = false;  // the maximize button shows "restore"
  bool pinned = false;     // the pin button shows the pushed-in, upright pin
  int hover_id = -1;       // the button under the pointer, or -1
  int pressed_id = -1;     // the button being held down, or -1
};

// Draws `count` buttons that sit next to each other (the strip). Where the strip's lowest corners are is
// taken from the outermost buttons, so a strip at the right edge is rounded at its bottom right and one at
// the left edge at its bottom left; its top edge is meant to lie on the top edge of the titlebar.
void draw_caption_buttons(cairo_t* cr, const CaptionButton* buttons, int count, const CaptionState& state);

}  // namespace fleetwm::kit
