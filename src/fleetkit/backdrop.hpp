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
// `recheck` skips the two-second rest and looks at the wallpaper now: for the caller that was just told it changed.
cairo_surface_t* load_backdrop(bool recheck = false);

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
  double ray = 0.0;                   // peak opacity of the Windows 7 light ray across it (paint_glass_ray); 0 = none
};
// The Windows 7 light ray, fixed to the screen like a reflection on a pane: one diagonal beam across an output of out_w x out_h, a crisp
// bright leading edge with a long soft tail, leaning like a "/". A glass rectangle only shows the part of the beam that lies behind it, so
// a window dragged across the screen slides through the ray. (x, y, w, h) is the rectangle in `cr`'s coordinates and (screen_x, screen_y)
// is where its top-left corner is on the screen. `strength` is the beam's peak opacity. Painted over what is there, clipped to the rectangle.
void paint_screen_ray(cairo_t* cr, double screen_x, double screen_y, double x, double y, double w, double h, int out_w, int out_h,
                      double strength);
// Where an output of out_w x out_h shows a backdrop of bw x bh: cover-fit and centred, as the wallpaper itself (paint_backdrop uses the same).
struct BackdropMap {
  double scale, ox, oy;
};
inline BackdropMap backdrop_map(int bw, int bh, int out_w, int out_h) {
  const double sx = static_cast<double>(out_w) / bw, sy = static_cast<double>(out_h) / bh;
  const double s = sx > sy ? sx : sy;
  return {s, (out_w - bw * s) / 2, (out_h - bh * s) / 2};
}
struct BackdropBox {
  double x, y, w, h;
};
// The part of the backdrop (in its own pixels) that lies under the output-relative rectangle (x, y, w, h), kept inside the picture.
inline BackdropBox backdrop_source(const BackdropMap& m, int bw, int bh, double x, double y, double w, double h) {
  BackdropBox b{(x - m.ox) / m.scale, (y - m.oy) / m.scale, w / m.scale, h / m.scale};
  // half a pixel in from every edge, so bilinear sampling never reaches outside the picture (the renderers do not all clamp there)
  if (b.w > bw - 1) b.w = bw - 1;
  if (b.h > bh - 1) b.h = bh - 1;
  if (b.x < 0.5) b.x = 0.5;
  if (b.y < 0.5) b.y = 0.5;
  if (b.x + b.w > bw - 0.5) b.x = bw - 0.5 - b.w;
  if (b.y + b.h > bh - 0.5) b.y = bh - 0.5 - b.h;
  return b;
}

void paint_glass(cairo_t* cr, cairo_surface_t* backdrop, int out_w, int out_h, double screen_x, double screen_y, double x,
                 double y, double w, double h, const GlassStyle& style);

}  // namespace fleetwm::kit
