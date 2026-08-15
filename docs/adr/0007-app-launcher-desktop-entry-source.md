# ADR 0007: App launcher, powered by GDesktopAppInfo, spawned fresh per use

## Status
Accepted

## Context
The user wants a minimal Albert/rofi/dmenu-style app launcher: fuzzy-ish
search over installed applications, a short category/type hint per
result, and the ability to run arbitrary typed text as a shell command.
No such component, and no `.desktop`-file parsing, exists anywhere in the
repo yet. GTK4 already pulls in GLib/Gio transitively as part of its own
dependency chain.

## Decision
- New `fleetwm-launcher` GTK4 + `gtk4-layer-shell` popup process
  (`src/launcher/`), spawned fresh on each invocation by the compositor's
  existing keybind-spawn mechanism (`spawn()` in
  `src/compositor/input.cpp`, generalized from the prior
  `spawn_terminal()`), bound to Alt+D. Not a resident daemon -- avoids
  introducing a new compositor-to-client IPC direction that doesn't exist
  yet (ADR 0003 only covers client-to-compositor).
- Application enumeration, filtering (`NoDisplay`/`Hidden`/`OnlyShowIn`),
  and launching all go through GLib's `GDesktopAppInfo` API
  (`g_app_info_get_all()`, `g_app_info_should_show()`,
  `g_app_info_launch()`) rather than a hand-rolled desktop-entry-format
  parser. Gio is already an effectively-required transitive dependency of
  every fleetwm install that builds `fleetwm-bar`/`fleetwm-settings`, and
  it correctly handles field-code stripping, spec-compliant filtering, and
  locale-aware name resolution for free.
- Search is a simple case-insensitive substring match over name + comment,
  sorted by earliest match position then alphabetically -- no fuzzy
  subsequence scoring for MVP. Results render in a `GtkListBox` (not
  `GtkListView`), since expected app counts (typically under a few
  hundred) don't need list virtualization.
- A synthetic "Run Command" row always appears when the query is
  non-empty, launched via hand-rolled `fork()` + `execlp("/bin/sh", "sh",
  "-c", ...)`, matching the compositor's own existing spawn pattern.

## Consequences
- README's process count goes from five to six: `fleetwm-launcher` is
  "spawned on demand" like `fleetwm-settings`.
- `gio-2.0` becomes an explicit meson dependency in
  `src/launcher/meson.build`, though it was already transitively present
  via `gtk4_dep` everywhere GTK4 is used in this repo -- no new class of
  dependency is introduced, so (unlike the greeter's `-Dgreeter` gate)
  the launcher's `subdir()` call is unconditional, consistent with
  `bar`/`settings` also being unconditional.
- No new IPC surface: `gtk4-layer-shell`'s client-side
  `GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE` is sufficient for the popup to
  grab keyboard focus on its own.
- Deferred, not built now: icon rendering, frecency/history persistence,
  fuzzy subsequence scoring, any plugin modes (calculator, web search,
  window switcher), `Terminal=true` desktop-entry special-casing, and
  theme.toml/CSS integration.
