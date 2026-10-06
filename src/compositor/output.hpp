#pragma once

#include <wayland-server-core.h>

extern "C" {
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
}

#include <array>
#include <memory>
#include <ctime>

#include "workspace.hpp"

namespace fleetwm {

class Server;
class DebugOverlay;

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

  // Adaptive render throttling (RenderMode::Custom, theme.hpp): tracks
  // when this output last actually committed a frame, plus a lazily-
  // created one-shot timer that re-arms wlr_output_schedule_frame() once
  // the configured FPS interval has elapsed -- see output_frame()
  // (output.cpp) for the full mechanism. Unused/idle at zero cost
  // whenever RenderMode is Synced (the default) or this output currently
  // has a fullscreen view, which always bypasses the cap entirely.
  wl_event_source* fps_cap_timer = nullptr;
  timespec fps_cap_last_commit{};
  bool fps_cap_has_last_commit = false;

  Workspace& active_workspace() { return workspaces[active_workspace_index]; }

  // Recomputes usable_area from scratch: starts from the full output box
  // and shrinks it by every mapped layer-shell surface's anchored
  // exclusive zone on this output, then re-tiles via relayout() so tiled
  // windows immediately respect the new reservation. Matches this
  // codebase's existing pattern of eagerly re-deriving full state (see
  // relayout() itself) rather than incremental accounting.
  // After a mode change requested through apply_output_setting() the previous
  // mode is kept until the new one has presented a few frames. If the monitor
  // or driver cannot actually drive the new mode (commits fail repeatedly),
  // the compositor reverts on its own instead of leaving the screen dark.
  bool has_fallback = false;
  int fallback_width = 0, fallback_height = 0, fallback_refresh_mhz = 0;
  int commit_failures = 0, confirm_frames = 0;

  void update_usable_area();
  // Desktop layout: after the work area changes (taskbar moved/resized), pull
  // floating windows back inside it and refit maximized ones.
  void fit_floating_views();
  // Switching to the Desktop layout: put every tiled window where tiling had it
  // (master left half, stack on the right, no gaps), as snapped windows.
  void snap_tiled_windows();

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

  // Alt+Shift+I (default) performance overlay: see debug_overlay.hpp. frame_begin runs right before
  // the commit (the picture it repaints goes out with that commit), frame_end right after it.
  void debug_frame_begin(const timespec& now);
  void debug_frame_end(double cost_ms);

 private:
  std::unique_ptr<DebugOverlay> debug_overlay_;
};

}  // namespace fleetwm
