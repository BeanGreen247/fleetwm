#pragma once

// The tint engine: which colour glass surfaces (titlebars, window frames, the bar, menus, tooltips) are tinted with.
// The colour comes from the user's choice (Settings -> Theme -> Glass tint): Windows 7 sky blue, a picked colour, the
// dominant colour of the wallpaper, or none (the theme's own colours). apply_glass_tint() turns that into the three
// colours in Palette that every glass painter uses.

#include <cairo.h>

#include <cstdint>

#include "fleetkit.hpp"

namespace fleetwm::kit {

// The colour a wallpaper "is", for tinting: the hue that most of the colourful pixels share, at a saturation and a
// brightness that make glass look like glass (not a muddy brown, not a neon). Pixels are 8-bit R,G,B,A in memory order
// (A ignored). A grey picture gives a neutral grey-blue. Pure: no files, no cairo.
Color tint_from_pixels(const uint8_t* rgba, int w, int h);

// The same for the current wallpaper, from the small blurred copy the glass already uses (backdrop.hpp). Kept until the
// wallpaper changes. Falls back to the Windows 7 blue when there is no wallpaper.
Color wallpaper_tint();

// The Windows 7 sky blue used by the Aero mode.
Color aero_blue();

// How `base` and the theme colours combine into the glass colours at a given intensity (0..100). Pure.
void mix_glass_colors(Palette& pal, const Color& base, int intensity);

// Fills pal.glass_surface / glass_title / glass_title_idle from the configuration.
void apply_glass_tint(Palette& pal, const GlassTintConfig& cfg);

}  // namespace fleetwm::kit
