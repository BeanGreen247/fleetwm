#pragma once

// The frosted backdrop of the glass look: a small, heavily blurred copy of the wallpaper. It is
// made once (the first time anything needs it after the wallpaper changed) and cached as a tiny
// PNG, so showing glass costs one small texture and no blurring at draw time.

#include <cairo.h>

#include <cstdint>

#include "fleetkit.hpp"

namespace fleetwm::kit {

// Box blur over RGBA pixels (premultiplication does not matter for opaque input), `radius` px,
// `passes` times (three passes approximate a Gaussian). Edges clamp.
void box_blur_rgba(uint8_t* rgba, int w, int h, int radius, int passes);

// Shrinks RGBA to at most `max_w` wide, keeping the aspect ratio (box filter).
void downscale_rgba(const uint8_t* src, int sw, int sh, int max_w, uint8_t** dst, int* dw, int* dh);

// The blurred wallpaper as a cairo surface (caller owns it), or nullptr when there is none (no
// wallpaper and nothing to fall back to). The caller owns one reference; the picture itself is shared
// within the process and re-checked against the wallpaper every couple of seconds. A solid-colour wallpaper gives a flat surface.
cairo_surface_t* load_backdrop();

// Same, without the per-process copy (reads the cached PNG, or makes it). load_backdrop() wraps this.
cairo_surface_t* load_backdrop_uncached();

// Paints the part of the backdrop that lies behind the rectangle (x, y, w, h) of an output of
// out_w x out_h px, using the same cover-fit the wallpaper uses, then lets the caller tint it.
void paint_backdrop(cairo_t* cr, cairo_surface_t* backdrop, int out_w, int out_h, double screen_x, double screen_y,
                    double x, double y, double w, double h);

// The glass look in one call, cheap enough to do on every redraw: the frosted backdrop behind the
// rectangle, a tint over it, a soft sheen across the top, and a thin light rim with a dark line
// just inside. No blur happens here (the backdrop is already blurred) and nothing is animated.
struct GlassStyle {
  Color tint{0.12, 0.14, 0.20, 1.0};  // usually the theme's background
  double tint_alpha = 0.58;           // how much of the backdrop shows through
  double radius = 0;                  // corner radius of the rectangle
  bool rim = true;
};
void paint_glass(cairo_t* cr, cairo_surface_t* backdrop, int out_w, int out_h, double screen_x, double screen_y, double x,
                 double y, double w, double h, const GlassStyle& style);

}  // namespace fleetwm::kit
