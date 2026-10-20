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

// A number that changes when what lies under the four frame parts `rects` (layout coordinates) changed: the windows below that touch them, their
// positions, sizes and the serial of their last content commit. Equal keys mean the picture would come out the same.
uint64_t glass_backdrop_key(Server* server, const View* w, const wlr_box rects[4]);

// Builds the four pictures. `area` is the output's box (the wallpaper is laid out by it). Returns false when nothing could be built.
bool glass_build_pictures(Server* server, const View* w, const wlr_box rects[4], const wlr_box& area, GlassPicture out[4]);

void glass_picture_release(GlassPicture* p);

}  // namespace fleetwm
