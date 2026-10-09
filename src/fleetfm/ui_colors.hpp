#pragma once

#include "fleetkit.hpp"
#include "fm_settings.hpp"
#include "view_style.hpp"

namespace fleetwm::fm {

using kit::Color;

// Every colour the window uses, per style and scheme. A style is drawn in its own colours (Windows 7 in its pale blues, Caja
// in GTK greys ...); "follow theme" takes the Fleetwm palette instead and keeps the style's layout.
struct Colors {
  Color window, text, text_dim, text_off, accent, accent_text, danger, ok;
  Color chrome_top, chrome_bot, chrome_border;       // toolbar and address rows
  Color nav_bg, nav_text, nav_head;                  // navigation pane
  Color content, content_alt;                        // file list
  Color head_top, head_bot, head_sep;                // column headings
  Color sel_top, sel_bot, sel_border, sel_text;      // selection (window focused)
  Color selx_top, selx_bot, selx_border;             // selection (unfocused)
  Color hot_top, hot_bot, hot_border;                // hover
  Color btn_top, btn_bot, btn_border, btn_hot_top, btn_hot_bot, btn_down_top, btn_down_bot;
  Color field, field_border;                         // text boxes
  Color tab_on, tab_off, tab_border;
  Color status_top, status_bot;
  Color bar_top, bar_bot, bar_border, bar_track, bar_warn_top, bar_warn_bot;  // progress / free space
  Color menu_bg, menu_border, menu_hot, shadow;
  Color scroll_track, scroll_thumb;
  bool dark = false;
};

Colors make_colors(ViewStyle style, ColorScheme scheme, const kit::Palette& theme);

}  // namespace fleetwm::fm
