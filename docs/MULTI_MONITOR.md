# Multi-monitor design (2026-10-08)

Status: stages 1 to 4 are built and were run on the headless rig with two and then three outputs (2026-10-08); stages 5 and 6 and the
real-hardware risks are open. The measurement of the starting point is kept below. Cross-reference of dwm and i3: memory bank note
`dwm-multi-monitor-crossref-2026-10-07`.

## What happens today (measured, headless rig, two outputs)

Two headless outputs (`WLR_HEADLESS_OUTPUTS=2`, HEADLESS-1 1280x720 at 0,0 and HEADLESS-2 1024x768 at 1280,0, both set with `OUTPUT_SET`), then
`fleetwm-bar` and `foot` started:

- The compositor handles two outputs: each has its own scene output and usable area; `OUTPUT_SET` places them (`outputs.toml` keeps the layout).
- The bar and the wallpaper ask for no particular output (`Surface::Config::output` is null in both), so the compositor picks one: everything
  landed on HEADLESS-2 and HEADLESS-1 stayed black, no bar, no wallpaper.
- A new window opened on the same output (the focused one) at the cascade position of that output's work area.
- `View::output`, `Output::usable_area` and `Server::active_workspace_for_focused_output()` already exist, so the compositor side has the per-output
  concepts; the programs (bar, wallpaper) do not.

## Goals and non-goals

Goals: every screen has a bar (taskbar), a wallpaper and a work area; windows open on the focused output and can be moved between outputs by
keyboard and by dragging; unplugging a screen never loses a window; plugging it back restores the layout.
Non-goals for the first version: per-output scale mixing beyond what wlroots gives, per-output themes, mirrored outputs, spanning one window over two.

## Model

Desktop layout (floating): one set of workspaces shared by all outputs, a window belongs to one output (`View::output`), the work area is per output.
This is the Windows model and the simplest. Tiling layout: dwm/i3 style, per-output workspace set (each output shows its own workspace; selecting one
changes the focused output only), which needs `Server` to hold `workspace per output` instead of one active workspace. Keep that second part for the
tiling stage; the desktop stage does not need it.

## Stages (each one testable on the headless rig with `WLR_HEADLESS_OUTPUTS=2`)

1. **A bar and a wallpaper per output.** Bind `wl_output` at version 4 (it carries `name`) or use xdg-output, so a program can tell outputs apart and
   pass `Surface::Config::output`. Two ways: (a) one `fleetwm-bar` process per output, started by the compositor for each output with `--output NAME`
   and stopped when it goes away (the bar's `B` state is one-output by construction, so this is the small change); (b) one process that owns a bar per
   output (cleaner at idle: one process, one set of timers, but `B` becomes a vector). Prefer (a) first; measure the idle cost of the second process
   (section 11 of PERFORMANCE_FINDINGS: a bar is about 1 wake-up/s, 9 ms CPU per 40 s) before choosing (b). The wallpaper already holds `std::vector<Output*>`
   and only needs one layer surface per output. Tests: bar and wallpaper on both outputs in a screenshot per output (`grim -o`), process count follows
   hotplug (`OUTPUT_SET` on and off, headless outputs can be added with `wlr_headless_add_output` through a debug IPC command).
2. **Windows follow the focused output.** New windows open on the output of the focused window (or the pointer's output when nothing is focused), the
   cascade runs per output, `maximize` and snapping use that output's work area (already `output->usable_area`). The taskbar of each output shows the
   windows on that output by default, with a setting "all windows on every taskbar". Check: window list per output via the IPC `WINDOWS` command.
3. **Moving between outputs.** Keyboard: `Super+Shift+Left/Right` sends the focused window to the output on that side (directional, by output
   position; ring order as fallback with `Super+Shift+,` and `.`, dwm's `tagmon`); `Super+Left/Right` is already snapping, so the directional move is
   used only when the window is already snapped to that edge (Windows behaviour: `Win+Shift+Left`). Drag: a window dragged across the edge changes
   `View::output` when more than half of it is on the other output. Keep the window's position relative to the work area, clamp into the new one.
4. **Hotplug.** On remove: windows of that output move to the first remaining output (dwm's rule) and remember `last_output_name`; on add, windows
   whose remembered name matches move back and the saved position in `outputs.toml` is applied. The bar and wallpaper of the removed output go away
   with it. Check: remove and add `HEADLESS-2` with windows open; window count never changes.
5. **Tiling per output** (only if the tiling layout stays supported): workspace set per output, `Super+,`/`.` focus the other output, pointer moves
   focus between outputs.
6. **Settings -> Display**: it already arranges screens by dragging; add "Primary screen" (where the start menu and new windows go when nothing is
   focused) and the per-output taskbar choice.

## Risks to check on real hardware (cannot be settled on the headless rig)

- Real DRM outputs with different scales or refresh rates: frame timing per output, the busy cursor and hardware cursor plane on the second output.
- Hotplug races: a monitor that disappears while a layer surface is mapped; wlroots 0.18 destroys the `wlr_output` while clients still hold `wl_output`.
- Screen lock and idle: the locker must cover every output (one lock surface per output, check `ext-session-lock` handling) and DPMS per output.
- Pointer between outputs of different sizes (dead corners): wlroots layout clamps the cursor to the union of outputs.

## Effort and order

Stages 1 and 2 give most of the value (a usable second screen) and are about a day each including tests; 3 and 4 follow; 5 only on demand. The first step
is stage 1(a), since it needs no change to the bar's internal state.

## Built (2026-10-08) and how it was checked

- Stage 1: wl_output is bound at version 4 (connector name), `fleetwm-bar --output NAME` and `fleetwm-wallpaper --output NAME`; the compositor starts one of
  each per output when the session is up and for every output plugged in later, and ends them with the output (`Server::start_output_helpers`,
  `stop_output_helpers`). Layer surfaces that ask for no output (menus, the mixer, tooltips of a bar with `--output`) go to the output with the pointer
  (`Server::focused_output`). Run: two outputs gave two bars and two wallpapers, a screenshot per output shows a bar on each.
- Stage 2: new windows open on the output with the pointer; the window list carries the screen (`2@HEADLESS-2` in the workspace field, old form still parses) and
  a bar started with `--output` lists only its screen's windows. Workspaces are one set for all screens in the Desktop layout (`switch_workspace_everywhere`),
  per screen in Tiling. Run: a foot opened with the pointer on HEADLESS-2 appeared on that bar only.
- Stage 3: the keyboard move already existed (Super+arrows at the edge); it and the new drag-across-the-edge (`adopt_output_under` at the end of a move drag) share
  `transfer_view_to_output`. The drag was not driven on the rig, only the keyboard path shares its code.
- Stage 4: removing an output moves its windows to the first other output and remembers the name (`evacuate_output`); adding one with the same name moves them
  back (`restore_output_windows`). Run: `DEBUG_OUTPUT_REMOVE HEADLESS-2` moved the foot to HEADLESS-1 and ended that bar and wallpaper; `DEBUG_OUTPUT_ADD`
  started a new pair. The return path was not exercised: headless outputs get a new name every time.
- Test hooks: `FLEETWM_DEBUG_OUTPUTS=1` in the compositor's environment enables the IPC commands `DEBUG_OUTPUT_ADD <w> <h>` and `DEBUG_OUTPUT_REMOVE <name>`.
