#pragma once

struct wlr_buffer;

namespace fleetwm {

// The picture the drop shadow pieces are cut from (kit::draw_window_shadow), one per focus state, kept for the process. The caller
// gets a lock it releases with wlr_buffer_unlock(); null when memory ran out.
wlr_buffer* shadow_picture(bool focused);
// The glass or the theme changed nothing about it today; kept for symmetry with glass_backdrop_clear() if the picture ever depends on the theme.
void shadow_clear();

}  // namespace fleetwm
