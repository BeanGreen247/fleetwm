#pragma once

// Where a small popup (the volume mixer) goes so that it opens next to the bar or taskbar it was opened
// from: the same edge, clear of it, in the right-hand corner where the volume readout is. The mixer used
// to be pinned to the top right whatever the layout, which in the Desktop layout (taskbar at the bottom)
// put it on the other side of the screen. Pure arithmetic so the unit tests can cover every combination.

#include "bar_config.hpp"
#include "theme.hpp"

namespace fleetwm {

// The layer-shell anchor bits (wlr-layer-shell-unstable-v1), spelled out so this header needs no protocol header.
enum PopupAnchor : unsigned { kAnchorTop = 1, kAnchorBottom = 2, kAnchorLeft = 4, kAnchorRight = 8 };

struct PopupSpot {
  unsigned anchor = 0;
  int top = 0, right = 0, bottom = 0, left = 0;  // margins from the anchored edges, px
};

inline PopupSpot popup_spot_beside_bar(WindowLayout layout, BarLayout bar, TaskbarPosition taskbar, int gap = 6,
                                       int screen_edge = 8) {
  PopupSpot s;
  if (layout == WindowLayout::Desktop) {
    switch (taskbar) {
      case TaskbarPosition::Top:
        s.anchor = kAnchorTop | kAnchorRight;
        s.top = kTaskbarThickness + gap;
        s.right = screen_edge;
        break;
      case TaskbarPosition::Left:
        s.anchor = kAnchorLeft | kAnchorBottom;
        s.left = kTaskbarWidth + gap;
        s.bottom = screen_edge;
        break;
      case TaskbarPosition::Right:
        s.anchor = kAnchorRight | kAnchorBottom;
        s.right = kTaskbarWidth + gap;
        s.bottom = screen_edge;
        break;
      case TaskbarPosition::Bottom:
      default:
        s.anchor = kAnchorBottom | kAnchorRight;
        s.bottom = kTaskbarThickness + gap;
        s.right = screen_edge;
        break;
    }
    return s;
  }
  // Tiling: the bar is along the top; Capsules and Island float a little below the screen edge.
  s.anchor = kAnchorTop | kAnchorRight;
  s.top = kBarHeight + gap + (bar == BarLayout::Capsules ? kCapsuleTopMargin : bar == BarLayout::Island ? kIslandTopMargin : 0);
  s.right = screen_edge;
  return s;
}

}  // namespace fleetwm
