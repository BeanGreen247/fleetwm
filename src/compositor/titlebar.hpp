#pragma once

#include <string>

#include "theme.hpp"
#include "window_geometry.hpp"

struct wlr_buffer;

namespace fleetwm {

// Server-side titlebar used by the Desktop window layout. Rendered with cairo
// into a CPU buffer that the compositor shows as a scene buffer; it is only
// re-rendered when the title, focus, maximized/pinned state, width, settings or
// hovered button change, so an idle desktop costs nothing.

// Plain-struct copy of the [titlebar] settings for the layout maths.
geom::TitlebarMetrics titlebar_metrics(const TitlebarConfig& cfg);

// Reloads the colors used by render_titlebar() from the theme CSS.
void titlebar_reload_palette(const ThemeConfig& theme);

// The window-backdrop color (theme background) as RGBA floats.
void titlebar_backdrop_color(float rgba[4]);

struct TitlebarState {
  std::string title;
  bool focused = false;
  bool maximized = false;
  bool pinned = false;
  int hover_button = geom::kBtnNone;
  bool glass = false;  // theme.toml glass_effects: translucent, sheen, round glossy buttons
};

// Returns a new buffer (caller owns one reference: wlr_buffer_drop() it once
// handed to a scene node), or nullptr on failure.
wlr_buffer* render_titlebar(int width, const TitlebarState& state, const TitlebarConfig& cfg);

// One side of the window frame: edge 0 = left, 1 = right, 2 = bottom. Same ownership as above.
wlr_buffer* render_frame_strip(int width, int height, int edge, bool focused, bool glass);

}  // namespace fleetwm
