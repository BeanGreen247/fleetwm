# ADR 0004: GTK4 + gtk4-layer-shell for the bar and settings app

## Status
Accepted

## Context
The bar and settings/power-menu app need a UI toolkit. Candidates
considered: raw Wayland + cairo/pango (smallest footprint, full control,
but every widget hand-built), raw Wayland + EGL + a custom 2D renderer
(best perf ceiling, but building a mini-toolkit before building the
product), GTK4, and Qt6.

## Decision
GTK4 + `gtk4-layer-shell` for both binaries. CSS-based theming maps
directly onto the rounded/sharp-corner toggle, the 5 named themes, and
runtime accent-color swapping (`border-radius` and `@define-color` cover
essentially all of it with zero custom render code). `GtkColorButton`
covers the custom accent-color picker for free. `gtk4-layer-shell-dev` is
a direct repo package on Debian 13.6 / Ubuntu 26.04, so no vendoring is
needed to reach v1 on the target distros. Idle RSS for a bar this simple
is realistically 15-30MB -- a real cost against the "minimal" goal, but
outweighed by the build-velocity win for v1.

## Consequences
- The bar and settings app are two separate GTK4 processes (see
  `src/bar/meson.build`, `src/settings/meson.build`), not one, so the
  always-resident bar's binary and resident set stay free of the settings
  app's heavier one-shot code paths (color picker, image decode for accent
  auto-extraction).
- A documented v2 stretch path exists: rewrite the bar only (not settings,
  since it's rarely invoked and its footprint doesn't matter) in raw
  Wayland + cairo once the UX/feature set has stabilized enough to be
  worth hand-optimizing. Not attempted before v1 ships.
- Qt6 was not chosen because its layer-shell story is less turnkey in
  stock Debian/Ubuntu repos than `gtk4-layer-shell`, despite comparable
  theming capability.
