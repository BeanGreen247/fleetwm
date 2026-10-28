#pragma once

struct wl_display;

namespace fleetwm {
// Offers wp_fifo_manager_v1 and wp_commit_timing_manager_v1 (accepted, not enforced), see presentation_hints.cpp.
void presentation_hints_init(wl_display* display);
}  // namespace fleetwm
