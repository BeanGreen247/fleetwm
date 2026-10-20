#pragma once

// The parts of the Windows 7 desktop that are not window frames: the taskbar button tile, the start orb, the show-desktop sliver and the
// opaque ("Basic", no glass) taskbar. Pure cairo drawing so the bar, the tests and the preview tools share it. Measured on real
// Windows 7 screenshots (docs/WINDOWS7_FRAME_REFERENCE.md, fifth pass).

#include <cairo.h>

#include "fleetkit.hpp"

namespace fleetwm::kit {

struct TaskbarTile {
  bool running = false;  // the program has a window: its button is a tile (an idle pin shows nothing until the pointer is on it)
  bool focused = false;  // the active window: a brighter tile
  bool hover = false;
  bool pressed = false;
};

// One taskbar button: a 1 px dark outline, a 1 px light line inside it and a glass gradient that is light in the top half. Over the taskbar
// glass (or the Basic taskbar) it reads as a pane of glass. `glow_x` is where the pointer is inside the tile (a soft light follows it).
void draw_taskbar_tile(cairo_t* cr, double x, double y, double w, double h, const TaskbarTile& st, double glow_x);

// The round start button, centred on (cx, cy) with radius r: dark rim, light inner rim, blue glass body, a glossy highlight over the top
// half and a four-pane mark in the middle, its panes light and dark variants of `accent` (the theme's colour).
void draw_start_orb(cairo_t* cr, double cx, double cy, double r, const Color& accent, bool hover, bool pressed);

// The thin button at the far end of the taskbar that shows the desktop.
void draw_show_desktop_sliver(cairo_t* cr, double x, double y, double w, double h, bool hover);

// The opaque taskbar of the Basic (no glass) look: the tint darkened into a vertical gradient with a light line under the dark top edge.
// `top_edge` is the side facing the desktop: 0 top, 1 bottom, 2 left, 3 right.
void paint_basic_taskbar(cairo_t* cr, double w, double h, const Color& tint, int top_edge);

}  // namespace fleetwm::kit
