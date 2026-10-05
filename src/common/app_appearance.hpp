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

}  // namespace fleetwm
