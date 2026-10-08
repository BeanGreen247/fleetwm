#pragma once

// The frosted backdrop behind Fleetwm's own windows (Settings, Shortcuts, the language picker) when Glass effects is on:
// the blurred wallpaper of the whole output as one picture, kept per output size, of which a window shows the part that
// lies behind it. The programs paint a see-through background on top (Ui::set_translucent), so the window looks frosted
// and still moves, resizes and changes screens like any other window: the compositor re-aims the slice, nothing is blurred again.

struct wlr_buffer;

namespace fleetwm {

// The backdrop picture for an output of out_w x out_h logical pixels (a new reference the caller drops), or nullptr when there is
// no wallpaper to frost. The same picture is returned for the same size until the wallpaper changes.
wlr_buffer* glass_backdrop_for(int out_w, int out_h);
// Drops every kept picture (the glass setting or the wallpaper changed).
void glass_backdrop_clear();

}  // namespace fleetwm
