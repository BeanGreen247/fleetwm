# ADR 0001: wlroots over Smithay as the compositor foundation

## Status
Accepted

## Context
Fleetwm needs a Wayland compositor library to build on rather than
implementing the core protocol, DRM/KMS, and libinput handling from
scratch. The two mature options are wlroots (C) and Smithay (Rust).

## Decision
Use wlroots. Smithay is Rust-only with no supported C++ embedding path,
which rules it out for a C++ project outright. wlroots is a C library with
straightforward C++ interop (it's already what Sway, Wayfire, and
Hyprland's earlier C++ codebase built on), is the most mature/
batteries-included option (KMS/DRM, libinput, XWayland, scene-graph
rendering all provided), and is packaged in Debian 13.6 / Ubuntu 26.04
repos at a recent enough version (0.18 series) to avoid the ABI-skew pain
older distro releases would have introduced.

## Consequences
- Compositor code is written against wlroots' C API from C++ (see
  `src/compositor/server.hpp` for the wrapping pattern: free C-ABI
  listener trampolines as `friend` functions of `Server`, delegating to
  typed member functions).
- Minimum supported wlroots version is pinned at 0.17 in
  `src/compositor/meson.build`, with 0.18 as the primary target.
