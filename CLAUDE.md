# fleetwm — notes for Claude

## Backlog (noted 2026-08-16)

Not yet built, but listed in README's "target v1 scope":
- Settings app — Default Apps tab: per-category default application
  picker (browser, terminal, file manager, image viewer, etc.), backed
  by `xdg-mime`/`mimeapps.list` so OS-level and other XDG-aware apps see
  the same defaults fleetwm sets.
- Power menu: Sleep, Reboot, Poweroff actions (Settings currently has a
  Power *profile* tab for battery machines — Normal/Performance/Battery
  Saver via `powerprofilesctl` — that's not the same thing as session
  power actions).

Performance work requested, not yet investigated — profile/measure on
real hardware before implementing, don't assume a fix is needed without
data:
- Multi-monitor setups.
- Fullscreen app / gaming performance (check whether `wlr_scene` is
  already taking a compositing-bypass/direct-scanout path for fullscreen
  clients before assuming custom code is needed).
- Heavy GPU task performance generally.
- General CPU efficiency — standing goal, not a one-off task.
- General GPU efficiency — standing goal, not a one-off task.
- Memory usage — standing goal: keep resident memory low across all of
  fleetwm's processes (compositor, bar, wallpaper, greeter), not just
  CPU/GPU cycles.

The dev VM (`ssh fleetwm-dev`, see local Claude memory for details) has
no GPU acceleration at all (`WLR_RENDERER_ALLOW_SOFTWARE=1` is required
just to get it to render) — it cannot be used to measure GPU/fullscreen/
gaming performance. That needs real laptop/desktop hardware.

## Where else this is tracked

This repo has no other issue tracker or roadmap file (checked
`gh issue list` — empty; no `docs/ROADMAP.md` or similar exists despite
README referencing "the project plan"). The Claude session working on
this repo also keeps a local, machine-specific memory bank under
`~/.claude/projects/-home-bean-fleetwm/memory/` with much more detail
(architecture decisions, VM workflow gotchas, past bug fixes) — this file
is the git-tracked subset of that, kept intentionally short. When adding
to one, add to the other too so neither drifts out of sync.
