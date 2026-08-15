# ADR 0008: Add wlr_layer_shell_v1 compositor support

## Status
Accepted

## Context
fleetwm-launcher (ADR 0007) uses gtk4-layer-shell client-side, and future
fleetwm-bar/fleetwm-settings popovers do too (ADR 0004 chose
gtk4-layer-shell as the toolkit for both), but the compositor never
implemented the corresponding `wlr_layer_shell_v1` protocol. This was
discovered via real testing on fleetwm-dev: fleetwm-launcher failed
outright with "compositor does not support the Layer Shell protocol" /
"Failed to initialize layer surface", while plain `xdg_toplevel` clients
(foot) were unaffected. The protocol XML/codegen scaffolding was already
vendored preemptively (`protocols/wlr-layer-shell-unstable-v1.xml`) but
unused -- no `wlr_layer_shell_v1_create()` call existed anywhere.

## Decision
Add `wlr_layer_shell_v1_create(display_, 4)` plus a `new_surface`
listener in `Server::init()`, a new `LayerSurface` class
(`src/compositor/layer_surface.{hpp,cpp}`) mirroring `View`'s
listener-registration pattern, and a 5-tree scene-graph layering scheme
(background/bottom/toplevels/top/overlay) as permanent, always-enabled
`wlr_scene_tree`s parented directly under the root scene tree --
structurally outside the `Workspace`/`WorkspaceArray` model, so layer
surfaces persist across workspace switches without any change to
`Output::switch_workspace()`. A new `SceneNodeOwner` tag
(`src/compositor/scene_node_owner.hpp`), stored as the first member of
both `View` and `LayerSurface`, lets hit-testing safely disambiguate scene
nodes before reinterpreting them.

Scope is deliberately minimal: full output-usable-area exclusive-zone
accounting, multi-output layer-surface targeting beyond "first output",
and layer-surface popup lifecycle are deferred until the bar/settings app
actually need them. This pass only builds what's needed for an
OVERLAY-layer, unanchored, keyboard-exclusive popup (fleetwm-launcher) to
render and receive both keyboard and pointer input correctly.

## Consequences
- Unblocks fleetwm-launcher immediately -- first real end-to-end
  verification that a gtk4-layer-shell client works against fleetwm.
- Introduces the first scene-tree z-layering scheme in the compositor;
  existing `View` scene-tree creation moves from being a direct child of
  `scene_->tree` to living inside a dedicated `layer_toplevels_`
  sub-tree (one-line change in `server_new_xdg_toplevel`) -- a structural
  precedent future layering work (e.g. fullscreen-above-top handling) can
  build on.
- Lays groundwork for fleetwm-bar's future exclusive-zone reservation
  (the BOTTOM/TOP trees and per-surface `wlr_scene_layer_surface_v1`
  wiring already exist) without building the actual usable-area-shrinkage
  math yet -- that's a follow-up when the bar is built.
- Adds a second scene-tree hit-testing path (`scene_node_at()` generalizes
  the previous `view_at()`) in `server.cpp`'s cursor handling, the one
  piece of genuinely new (non-mirrored) logic in this change -- pointer
  clicks on a layer surface are forwarded but never trigger
  `focus_view`-style raise/activate, since layer surfaces are already top
  of their own layer and keyboard-exclusive focus (if requested) is
  granted at map time, not on click.
