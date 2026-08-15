# ADR 0002: Per-output workspaces (Sway/i3/dwm model)

## Status
Accepted

## Context
Multi-monitor workspace behavior needed deciding: a single tiling canvas
spanning all monitors as one region, versus each monitor owning its own
independent set of workspaces. The user explicitly requested the latter,
referencing prior use of Sway, i3, and dwm and wanting window placements to
persist exactly as left when switching away from and back to a workspace.

## Decision
Each `Output` owns its own set of 10 `Workspace` objects (keys 1-9, 0).
Switching the active workspace on one output has no effect on any other
output. Layout state (window positions/stacking) is retained per
workspace, so leaving and returning to a workspace restores it exactly.

## Consequences
- `Server::active_workspace_for_focused_output()` exists because "the
  active workspace" is only meaningful relative to a specific output, not
  globally -- there's no single global "current workspace" concept.
- The IPC protocol's `WORKSPACE N` / `WORKSPACE?` commands operate on
  whichever output currently has input focus, matching how Sway's `swaymsg
  workspace N` implicitly targets the focused output.
- Idle-monitor GPU/CPU cost stays near zero regardless of this choice --
  wlroots only redraws an output on frame damage, independent of the
  workspace model, so no extra design was needed to satisfy the
  performance goal.
