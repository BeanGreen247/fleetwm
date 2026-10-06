#pragma once

// The window caption buttons (pin, minimize, maximize/restore, close) drawn as a joined, glossy strip in
// the style of Windows 7: two-tone glass buttons for pin/minimize/maximize, a red one for close, a dark
// outline with a light inner edge, outlined glyphs, a glow behind a hovered button. The glass takes its
// colour from the theme (the titlebar background, the text colour and the accent), so the strip fits a dark,
// an OLED black or a light theme, and in glass mode the buttons are translucent like the bar they sit on.
// Everything is drawn with cairo paths and gradients (no images). The compositor draws its titlebars with it
// and the unit tests render it to check the result.

#include <cairo.h>

#include "fleetkit.hpp"

namespace fleetwm::kit {

struct CaptionButton {
  int id = -1;  // geom::TitleButton (window_geometry.hpp): 0 maximize, 1 close, 2 minimize, 3 pin
  double x = 0, y = 0, w = 0, h = 0;
};

// The theme colours the strip is made from (defaults: the dark theme).
struct CaptionColors {
  Color bg{0.094, 0.094, 0.145, 1};      // the titlebar's background
  Color fg{0.804, 0.839, 0.957, 1};      // the text colour: glyphs are drawn in it, so they suit the theme
  Color accent{0.537, 0.706, 0.980, 1};  // the glow and the lit colour of a hovered or pinned button
};

struct CaptionState {
  CaptionColors colors;
  bool glass = false;      // translucent buttons that let the wallpaper tint through, like the glass bar
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
