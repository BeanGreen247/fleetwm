#pragma once

#include <string>

#include "theme.hpp"

struct wlr_buffer;

namespace fleetwm {

// Server-side titlebar used by the Desktop window layout. Rendered with cairo
// into a CPU buffer that the compositor shows as a scene buffer; it is only
// re-rendered when the title, focus, maximized state, width or hovered button
// changes, so an idle desktop costs nothing.

constexpr int kTitlebarHeight = 32;
constexpr int kTitlebarButtonWidth = 38;

enum TitlebarButton { kButtonNone = -1, kButtonMaximize = 0, kButtonClose = 1 };

// Which button (if any) is under x (titlebar-local) in a titlebar `width` wide.
int titlebar_button_at(int width, double x);

// Reloads the colors used by render_titlebar() from the theme CSS.
void titlebar_reload_palette(const ThemeConfig& theme);

// Returns a new buffer (caller owns one reference: wlr_buffer_drop() it once
// handed to a scene node), or nullptr on failure.
wlr_buffer* render_titlebar(int width, const std::string& title, bool focused, bool maximized,
                            int hover_button);

}  // namespace fleetwm
