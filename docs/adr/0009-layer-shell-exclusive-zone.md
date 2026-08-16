# ADR 0009: Layer-shell exclusive-zone accounting

## Status
Accepted

## Context
ADR 0008 deliberately deferred "full output-usable-area exclusive-zone
accounting" until fleetwm-bar actually needed it. The bar reserves a top
strip of the screen (`gtk_layer_set_exclusive_zone`) that tiled windows
must not render underneath, matching standard i3bar/waybar behavior. Until
this change, `layer_surface_surface_commit` always passed
`usable_area == full_area` to `wlr_scene_layer_surface_v1_configure`, and
`Output::relayout()` tiled directly against the raw output box from
`wlr_output_layout_get_box()` -- an exclusive zone had no effect on
anything.

## Decision
- `Output` gains a `usable_area` member: the output's full box, shrunk by
  every currently-mapped layer surface's exclusive zone on that output.
- `Output::update_usable_area()` recomputes this from scratch on every
  call -- walks `Server::layer_surfaces`, skips anything not mapped to
  this output, and for each surface anchored to exactly one edge
  (spanning the perpendicular axis, per the wlr-layer-shell-v1 spec's own
  definition of what "exclusive zone" means) with a positive
  `exclusive_zone`, shrinks a running box from that edge. Matches this
  codebase's existing preference for eagerly re-deriving full state (see
  `Output::relayout()` itself) over incremental accounting -- simpler to
  reason about correctly, and the surface count here is always small.
- Called after `wlr_output_layout_add_auto` for a brand-new output (seeds
  `usable_area` to the full box before any layer surface exists), and
  again from `layer_surface_surface_commit`/`layer_surface_unmap`
  whenever a layer surface's mapped state or committed geometry could
  have changed the accumulated zone. It always ends by calling
  `relayout()`, so tiled windows immediately respect the new reservation.
- `Output::relayout()` changed its one `wlr_output_layout_get_box(...)`
  call to read `usable_area` instead.
- `Server::output_for(wlr_output*)` added as a small lookup helper --
  `LayerSurface` only knows the raw `wlr_output*` it's targeting
  (`layer_surface->output`), not the owning `Output*` wrapper.

## Consequences
- Pinned and floating views are unaffected (relayout already skips them
  entirely) -- only the tiled master-stack area shrinks.
- The first configure of a layer surface (inside `layer_surface_surface_commit`,
  gated on `initial_commit`) still passes `usable_area == full_area` for
  that one call, matching wlroots' own tinywl.c reference: a surface's own
  exclusive zone should not shrink the area it is itself positioned
  against. The real accumulated `usable_area` used by `relayout()` is
  computed separately, immediately after, via `update_usable_area()`.
- No exclusive-zone math for left/right-anchored bars is exercised yet
  (fleetwm-bar is top-only) but the anchor-direction branches exist for
  bottom/left/right too, following the same pattern, since the spec
  doesn't restrict exclusive zones to the top edge.
