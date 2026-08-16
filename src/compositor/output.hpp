#pragma once

#include <wayland-server-core.h>

extern "C" {
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
}

#include "workspace.hpp"

namespace fleetwm {

class Server;

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

  wl_listener frame{};
  wl_listener request_state{};
  wl_listener destroy{};

  Workspace& active_workspace() { return workspaces[active_workspace_index]; }

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
};

}  // namespace fleetwm
