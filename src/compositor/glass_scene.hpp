#pragma once

// What Windows 7's glass shows behind a window frame: the wallpaper AND the other windows below it (their frames and their content), all
// blurred. The scene graph cannot blur what it draws, so this builds the picture itself, small: for each part of a window's frame it draws,
// at a quarter of the size, the wallpaper, the frames of the windows below (as their glass colour) and their content (a few pixels read
// back from the client's texture), then blurs that with a small radius. The compositor shows the result under the translucent frame.

#include <cstdint>
#include <vector>

extern "C" {
#include <wlr/util/box.h>
}

struct wlr_buffer;

namespace fleetwm {

class Server;
class View;

inline constexpr int kGlassScale = 4;    // the picture is a quarter of the size
inline constexpr int kGlassMargin = 32;  // pixels of the surroundings it holds beyond the frame part, so a window can move a little before a rebuild

struct GlassPicture {
  wlr_buffer* buffer = nullptr;  // the blurred backdrop of `origin` (a reference this struct owns); null when there is none
  wlr_box origin{};              // the area of the layout it shows (the frame part grown by kGlassMargin), in layout pixels
  int w = 0, h = 0;              // buffer size in pixels (origin / kGlassScale)
};

// The windows below `w` (nearest the bottom first): the stacking order with the always-on-top and pinned layers above the rest, then everything under `w`.
std::vector<View*> glass_views_below(Server* server, const View* w);

// Per frame part (titlebar, left, right, bottom), a number that changes when what lies under it (the part grown by the margin, layout coordinates `rects`)
// changed: the windows below that touch it, their positions and sizes, and the serial of their last content commit. `layout` is the same without the content
// serials: if only `key` changed, only what the windows below show changed, not where they are, and the rebuild can wait a little longer. A key of 0 means
// no window is under that part, so the plain blurred wallpaper is exact and no picture is needed. The keys do not depend on where the glass itself is: the
// pictures are laid out in layout coordinates, so they stay right while the glass moves inside the area they hold (View::update_glass_backdrop).
struct GlassKeys {
  uint64_t key[4] = {0, 0, 0, 0};
  uint64_t layout[4] = {0, 0, 0, 0};
};
void glass_backdrop_keys(Server* server, const View* w, const wlr_box rects[4], GlassKeys* out);

// Builds the pictures of the frame parts in `mask` (bit i = part i; the others are left as they are). `area` is the output's box (the wallpaper is laid out by it).
// A new picture that comes out byte for byte like the one it replaces is dropped and the old one kept, so the scene sees no change and nothing is redrawn.
// Returns false when nothing could be built.
bool glass_build_pictures(Server* server, const View* w, const wlr_box rects[4], const wlr_box& area, GlassPicture out[4], unsigned mask = 15);

void glass_picture_release(GlassPicture* p);

}  // namespace fleetwm
