#pragma once

#include <string>

#include "theme.hpp"

// Makes GTK, Chromium and Qt applications follow the Fleetwm theme: dark for every theme
// except Light. GTK reads ~/.config/gtk-3.0/settings.ini and GSettings, Chromium and
// libadwaita read the colour-scheme setting (through xdg-desktop-portal-gtk), and Qt follows
// GTK through the gtk3 platform theme (QT_QPA_PLATFORMTHEME, set for the session).

namespace fleetwm {

inline bool theme_is_dark(ThemeName theme) { return theme != ThemeName::Light; }

// The text of a GTK settings.ini with `gtk-application-prefer-dark-theme` set for the
// theme: every other line, section and comment is kept as it was, the key is changed in
// place or added to the [Settings] section (created if missing).
std::string merge_gtk_settings(const std::string& existing, bool dark);

// A /bin/sh script that sets the GSettings colour scheme and, when the GTK theme is still
// a plain Adwaita one, switches between Adwaita and Adwaita-dark. A custom GTK theme the
// user picked is left alone. Needs gsettings; every command is allowed to fail.
std::string appearance_shell_script(bool dark);

// Writes the GTK 3 and GTK 4 settings files (merging with what is there) and runs the
// script above in the background. Cheap; callers only need to call it when the theme changes.
void apply_app_appearance(bool dark);

// Chromium-based browsers (Brave, Chrome, Chromium, Edge, Vivaldi) keep "Use system title bar
// and borders" as browser.custom_chrome_frame in each profile's Preferences file, and draw
// their own minimise/maximise/close buttons while it is true, whatever the compositor asks
// for. Returns the file text with the value set to `custom` (the key is changed in place);
// text without the key is returned as it was.
std::string set_custom_chrome_frame(const std::string& preferences, bool custom);

// Sets that value in every profile of the installed Chromium-based browsers: false while
// the Desktop layout draws the titlebars, true otherwise. A browser that is running is
// skipped (it would write its own value back on exit) and is picked up on the next call.
void apply_browser_frames(bool compositor_draws_titlebars);

}  // namespace fleetwm
