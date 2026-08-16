#pragma once

#include <gtk/gtk.h>

#include "theme.hpp"

namespace fleetwm::settings {

// Builds and drives the settings window's single page: dropdowns for the
// four ThemeConfig fields (theme.hpp/cpp already fully implements
// load/save -- this window is purely a GTK front end over that existing,
// working config layer, spawned fresh per invocation like the launcher).
// No live-apply to other running apps yet (nothing consumes theme.toml
// besides this window as of writing) -- see docs/adr for that follow-up.
class SettingsWindow {
 public:
  explicit SettingsWindow(GtkApplication* app);

 private:
  static void on_activate(GtkApplication* app, gpointer user_data);
  void build(GtkApplication* app);

  static void on_corner_style_changed(GtkDropDown* dropdown, GParamSpec*, gpointer user_data);
  static void on_theme_changed(GtkDropDown* dropdown, GParamSpec*, gpointer user_data);
  static void on_nav_mode_changed(GtkDropDown* dropdown, GParamSpec*, gpointer user_data);
  static void on_accent_auto_toggled(GtkCheckButton* button, gpointer user_data);
  static void on_accent_color_set(GtkColorButton* button, gpointer user_data);
  static void on_focus_border_thickness_changed(GtkSpinButton* button, gpointer user_data);

  // Writes config_ to disk. Called after every change rather than on a
  // separate "Apply" button -- theme.toml is cheap to rewrite and this
  // avoids a lost-edit foot-gun if the window is closed without an
  // explicit save step.
  void save();

  ThemeConfig config_;
  GtkWidget* window_ = nullptr;
  GtkWidget* accent_color_button_ = nullptr;
};

}  // namespace fleetwm::settings
