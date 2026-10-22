#pragma once

// The frosted backdrop behind Fleetwm's own windows (Settings, Shortcuts, the language picker) when Glass effects is on:
// the blurred wallpaper as one small picture (it is blurred, so about 480 px wide is all it has), which the scene scales up when a
// window shows the part that lies behind it; before 2026-10-22 it was painted out to the full output size per output (8.3 MB at 1080p). The programs paint a see-through background on top (Ui::set_translucent), so the window looks frosted
// and still moves, resizes and changes screens like any other window: the compositor re-aims the slice, nothing is blurred again.

struct wlr_buffer;

namespace fleetwm {

// The backdrop picture (a new reference the caller drops), or nullptr when there is no wallpaper to frost. The same picture is
// returned until the wallpaper changes.
wlr_buffer* glass_backdrop();

// Drops every kept picture (the glass setting or the wallpaper changed).
void glass_backdrop_clear();

}  // namespace fleetwm
