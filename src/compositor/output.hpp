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

  // Switches to workspace `index` (0-9) and requests a redraw. Layout of
  // views within the newly active workspace is Phase 1 scope (master-stack
  // tiling); Phase 0 just toggles scene-tree visibility per view so the
  // switch is at least observably correct.
  void switch_workspace(int index);
};

}  // namespace fleetwm
