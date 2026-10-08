# Windows 7 window frame: reference numbers and what Fleetwm uses (2026-10-08)

Measured from two Wikimedia Commons screenshots of real Windows 7 windows (fetched with the browser skill, pixel-measured with PIL; the originals are not stored in the repo):
- `File:Aero UI.png` (192x158, Aero, unscaled): caption buttons hang from the top edge of the window under a 1 px dark outline; minimize about 26-27 px wide, maximize about 27 px, close about 47 px (red, glossy); button height about 19-20 px; about 7 px of glass between the close button and the window's right outer edge.
- `File:Kalkulačka.jpg` (499x336, Win7 calculator, probably scaled to about 90%): side and bottom frame about 7 px of glass between a 1 px outline and a 1 px inner highlight; top (caption plus frame) about 27 px; title text left-aligned after the application icon.
Unscaled Windows 7 metrics (from the Win32 system metrics, not re-measured here): frame 8 px (SM_CXSIZEFRAME 4 + padded border 4), caption about 22-23 px, so the top is about 30 px.

Fleetwm defaults after this change (`TitlebarConfig`, `theme.toml` values the user already has stay): titlebar height 30, buttons 28 x 20 hanging from the top edge, close button 47 px (`kStripCloseScale` 1.68), title left-aligned, frame 8 px (`frame_px`), outer corners radius 7. Checked in the nested rig at 1280x720 and 1024x768.

Not done: the application icon in front of the title; Aero glass/blur as the default (glass is a setting, off in the check above).

## Second pass: glass on by default, corners, outline (same day)
- Corner radius: the outline of the unscaled `Aero UI.png` bends over 3-4 px (from (156,32) to (159,35)), the calculator shot (about 90%) over 3-4 px of its outer highlight: Fleetwm's `kWindowCornerRadius` is 4 now (was 7).
- Frame: 8 px on the sides and bottom = 1 px outer light line + 6 px translucent fill + 1 px inner dark line (measured on the Fleetwm render: x 172..179 left, y 524..531 bottom). The titlebar is 30 px (rows 0..29), its last row is the inner dark line, the side strips start at row 30.
- The outer line used to stop at the titlebar's and the bottom strip's ends; both now carry the same 1 px line (same colour and alpha as the side strips), so the outline is continuous down the left and right edges (checked at 14x and 18x zoom on a render over a vivid wallpaper, 1280x720 and 1024x768).
- `glass_effects` defaults to true (was false); an existing `theme.toml` keeps its own value.
