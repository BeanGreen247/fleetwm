#pragma once

// The glass reflection of Windows 7: one picture per output size holding the light ray (kit::paint_screen_ray) with a margin on both sides,
// so a window can show it shifted a little with its own position (DWM moves the reflection 10% with the window: a parallax that gives the
// glass depth). Kept at a quarter of the output size; the scene scales it up, the beam is soft anyway.

struct wlr_buffer;

namespace fleetwm {

inline constexpr int kRayScale = 4;  // the picture is out/4 in each direction

// The horizontal margin, in output pixels, the picture has on each side.
int glass_ray_margin(int out_w);
// A lock on the picture for an output of out_w x out_h logical pixels (release it with wlr_buffer_unlock), or nullptr when memory ran out.
wlr_buffer* glass_ray_for(int out_w, int out_h);
void glass_ray_clear();

}  // namespace fleetwm
