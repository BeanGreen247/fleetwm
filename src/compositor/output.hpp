#pragma once

#include <wayland-server-core.h>

extern "C" {
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
}

#include <array>
#include <ctime>

#include "workspace.hpp"

namespace fleetwm {

class Server;

// A fixed-capacity row of tiny rects rendering plain digits/letters via
// a hand-coded 3x5-pixel bitmap font (kDebugGlyphs, output.cpp) --
// deliberately not a real font/text-rendering library: this compositor
// draws zero text anywhere else, and the debug overlay's character set
// is small and fixed (digits, '.', and a handful of unit letters), so
// a real font stack would be a lot of new dependency for very little
// gained legibility. Rects are created once per row and reused on
// every update (recolored/hidden in place), same create-once-then-
// mutate pattern as Output's frame-time bar graph.
struct DebugTextRow {
  static constexpr int kMaxChars = 8;
  static constexpr int kGlyphCells = 15;  // 3 wide x 5 tall
  std::array<std::array<wlr_scene_rect*, kGlyphCells>, kMaxChars> cells{};
  bool created = false;
};

// One physical/virtual display. Owns its own WorkspaceArray (per-output
// workspace model, ADR 0002) so switching workspaces on one monitor never
// touches another's layout. Frame scheduling is entirely damage-driven via
// wlr_output's own frame event + wlr_scene_output_commit -- there is no
// polling timer here, which is what keeps an idle secondary monitor at
// near-zero GPU/CPU cost (see docs/adr/0004-idle-monitor-efficiency.md).
class Output {
 public:
  Output(Server* server, wlr_output* wlr_output_ptr);
  ~Output();

  Server* server;
  wlr_output* wlr_output_ptr;
  wlr_scene_output* scene_output = nullptr;

  WorkspaceArray workspaces = make_workspaces();
  int active_workspace_index = 0;

  // Output box minus every mapped layer-shell surface's exclusive zone
  // (e.g. fleetwm-bar's reserved top strip). Defaults to the full output
  // box when no layer surface claims an exclusive zone. Recomputed via
  // update_usable_area() whenever a layer surface on this output maps,
  // unmaps, or commits a changed exclusive zone -- see
  // layer_surface_surface_commit (layer_surface.cpp).
  wlr_box usable_area{};

  wl_listener frame{};
  wl_listener request_state{};
  wl_listener destroy{};

  Workspace& active_workspace() { return workspaces[active_workspace_index]; }

  // Recomputes usable_area from scratch: starts from the full output box
  // and shrinks it by every mapped layer-shell surface's anchored
  // exclusive zone on this output, then re-tiles via relayout() so tiled
  // windows immediately respect the new reservation. Matches this
  // codebase's existing pattern of eagerly re-deriving full state (see
  // relayout() itself) rather than incremental accounting.
  void update_usable_area();

  // Switches to workspace `index` (0-9), toggles scene-tree visibility per
  // view, and re-tiles the newly-active workspace via relayout().
  void switch_workspace(int index);

  // dwm/i3-style master-stack: the first (topmost-focused-first, per
  // Server::focus_view's splice-to-front) view in the active workspace's
  // tiled set becomes master and takes the left half of the output; the
  // rest split the right half into equal horizontal stripes. Pinned and
  // floating views are skipped entirely -- they keep whatever
  // position/size they already have. Safe to call any time the active
  // workspace's visible-view set changes (map/unmap/promote/float-toggle/
  // workspace-switch).
  void relayout();

  // Alt+Shift+I (default) debug overlay: a bottom-right bar graph of
  // this output's last kDebugBarCount frame times, colored green/
  // yellow/red against a 60Hz (16.6ms) budget. No text/fonts involved
  // -- this compositor does zero text rendering anywhere today (every
  // GTK4 client handles its own), and reusing the same wlr_scene_rect
  // machinery already used for window borders keeps this to plain
  // rectangles. Called every frame from output_frame() regardless of
  // whether the overlay is currently shown; it's a cheap no-op time-
  // stamp update when disabled (see update_debug_overlay()'s own doc
  // comment for why that matters).
  void update_debug_overlay();

 private:
  static constexpr int kDebugBarCount = 64;
  std::array<wlr_scene_rect*, kDebugBarCount> debug_bars_{};
  std::array<float, kDebugBarCount> debug_frame_times_ms_{};
  int debug_next_index_ = 0;
  bool debug_bars_created_ = false;
  bool debug_has_last_frame_time_ = false;
  timespec debug_last_frame_time_{};
  int debug_base_x_ = 0;
  int debug_base_y_ = 0;

  // Numeric text rows (avg frame time / compositor RSS / live CPU MHz)
  // stacked above the bar graph. Read/reformatted on a slower cadence
  // than the per-frame bar graph -- see kDebugTextIntervalMs (output.cpp)
  // -- both because /proc reads aren't free and because these numbers
  // don't meaningfully change frame to frame anyway.
  DebugTextRow debug_frame_time_row_;
  DebugTextRow debug_ram_row_;
  DebugTextRow debug_cpu_row_;
  timespec debug_last_text_update_{};
  bool debug_has_last_text_update_ = false;

  void create_debug_bars();
  void draw_debug_bars();
  void create_debug_text_rows();
  void update_debug_text();
};

}  // namespace fleetwm
