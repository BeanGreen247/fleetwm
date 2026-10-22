#pragma once

namespace fleetwm::kit {

// Box blur of 8-bit pictures: `passes` times a horizontal and a vertical average over 2r+1 pixels, the edge pixel repeated beyond the
// picture, each average rounded down. `channels` is 1 (a mask) or 4 (a picture); w is in pixels and stride in bytes. The result is
// byte for byte what the four copies of this loop gave before (glass backdrops, caption glow, window shadow, wallpaper), but each
// pass runs along rows (the vertical one keeps one running sum per column) and divides by a multiplication.
void box_blur_u8(unsigned char* p, int w, int h, int stride, int channels, int r, int passes);

}  // namespace fleetwm::kit
