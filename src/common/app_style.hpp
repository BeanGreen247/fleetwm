#pragma once

#include "theme.hpp"

namespace fleetwm {

// Loads the installed theme CSS stack (themes/*.css -- see themes_dir())
// for the current display and applies it at
// GTK_STYLE_PROVIDER_PRIORITY_APPLICATION, so every fleetwm GTK app
// (bar, launcher, settings) renders with the same background/accent/
// corner-radius look driven by theme.toml, instead of each binary
// hand-rolling its own CSS. Safe to call more than once (e.g. after a
// live theme reload) -- reuses the same provider and just reloads its
// content.
//
// The active theme file (dark.css, catppuccin.css, ...) already
// @imports base.css itself (see themes/base.css's own comment), so
// loading just the theme file pulls in the shared structural rules too.
// corners-{rounded,sharp}.css layers on top to set border-radius per
// corner_style, and a trailing override sets @accent_color to
// theme_config.accent.hex when the user picked an explicit color
// (auto_extract == false) -- each theme file's own @define-color
// accent_color is a per-theme default, not theme.toml-aware, so an
// explicit user choice must win over it. accent_extract.cpp (auto-
// extract from wallpaper) doesn't exist yet, so auto_extract == true
// currently just falls through to the theme's own default accent,
// matching themes/accent.css's own "left empty" comment for that case.
void apply_app_style(const ThemeConfig& theme_config);

}  // namespace fleetwm
