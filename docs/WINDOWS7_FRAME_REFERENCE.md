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

## Third pass: real desktop screenshots (same day)
Source: `File:Google Chrome Screenshot.png` (1066x725, unscaled Windows 7 Aero window incl. its glass frame, Wikimedia Commons, fetched through the Chrome debug browser and measured at 18x zoom). Findings that replaced the earlier guesses:
- The window edge is TWO lines that follow the rounded corners: a dark 1 px outline on the outside and a light 1 px line just inside it (left edge: x=13 dark, x=14 light, x=15..19 glass fill, x=20 inner light line, content from x=21: 8 px in all).
- The corner radius is about 6 px (the outline reaches the straight left edge about 6 px below the top and the top edge about 6 px to the right), on all four outer corners, for both lines. The previous version (radius 4, a straight light line clipped by the corner mask) looked like cut-off corners.
- Fleetwm now draws the outline with `kit::draw_window_edge` (radius `kWindowCornerRadius` = 6): dark line (black 60% on glass), light line inside it (white 60% on glass), in the titlebar (top corners), the side strips (straight) and the bottom strip (bottom corners), so the two lines run unbroken round the window.
