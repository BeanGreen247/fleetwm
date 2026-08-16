# ADR 0003: Line-based text IPC over a Unix domain socket

## Status
Accepted

## Context
The bar needs to send workspace-switch commands to the compositor and stay
in sync when workspace switches happen via keybind rather than a bar
click. The settings app needs to signal the bar and compositor to
live-reload theme state. Both need a simple, low-dependency mechanism.

## Decision
Two separate, deliberately different-weight mechanisms for two different
frequencies of use:

- **Workspace state** (frequent, needs request/reply and unsolicited
  push): a Unix domain socket at `$XDG_RUNTIME_DIR/fleetwm.sock`, with a
  newline-terminated ASCII line protocol -- `WORKSPACE N`, `WORKSPACE?`
  (compositor replies `N`), and an unsolicited `WORKSPACE_CHANGED N` push
  broadcast to all connected clients whenever the active workspace changes
  by any means. No JSON library dependency for a protocol this small.
- **Focused window title** (unsolicited push, added when fleetwm-bar was
  built): `FOCUSED_TITLE <text>`, broadcast to every connected client
  whenever keyboard focus changes (`Server::focus_view()` is the single
  function responsible for every focus transition, so this is one call
  site). `<text>` is the newly-focused view's title, falling back to its
  app_id, then empty, if unset; an empty `<text>` means no view is
  focused. Reuses the same socket/line protocol as `WORKSPACE*` rather
  than a separate mechanism, since it's the same shape of problem
  (frequent-ish, push-only, no reply needed).
- **Theme reload** (infrequent, fire-and-forget): `SIGUSR1` sent to the
  bar's PID (read from a pidfile at `$XDG_RUNTIME_DIR/fleetwm-bar.pid`)
  and to the compositor, after the settings app writes
  `~/.config/fleetwm/theme.toml`. No round-trip needed, so a signal is
  simpler and sufficient at this scale versus wiring up inotify watches.

## Consequences
- `src/common/ipc_client.hpp` implements the bar-side (and any future
  client's) half of the workspace socket protocol; `src/compositor/
  ipc_server.cpp` implements the compositor-side listener, both agreeing
  on `fleetwm::ipc_socket_path()` as the single source of truth for the
  socket path.
- The compositor accepts multiple simultaneous IPC connections (not just
  the bar) since nothing rules out other future clients (e.g. a CLI
  `fleetwm-msg` tool) wanting the same socket.
- The socket server is integrated into the compositor's own
  `wl_event_loop` via `wl_event_loop_add_fd` rather than a separate
  thread -- wlroots/Wayland server objects are not thread-safe, so all IPC
  handling must stay on the single compositor event loop thread.
