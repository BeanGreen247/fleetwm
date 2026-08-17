#pragma once

#include <gtk/gtk.h>

#include <array>

#include "app_style.hpp"
#include "bar_config.hpp"
#include "battery_source.hpp"
#include "default_apps.hpp"
#include "theme.hpp"
#include "wallpaper_config.hpp"

#include <vector>

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
  void apply_theme();

  static void on_theme_changed(GtkCheckButton* button, gpointer user_data);
  static void on_accent_auto_toggled(GtkCheckButton* button, gpointer user_data);
  static void on_accent_color_set(GtkColorButton* button, gpointer user_data);
  static void on_focus_border_thickness_changed(GtkSpinButton* button, gpointer user_data);
  static void on_focus_border_color_set(GtkColorButton* button, gpointer user_data);
  static void on_gap_changed(GtkSpinButton* button, gpointer user_data);
  static void on_pinned_border_thickness_changed(GtkSpinButton* button, gpointer user_data);
  static void on_pinned_border_color_set(GtkColorButton* button, gpointer user_data);
  static void on_pinned_focused_border_color_set(GtkColorButton* button, gpointer user_data);

  // Writes config_ to disk. Called after every change rather than on a
  // separate "Apply" button -- theme.toml is cheap to rewrite and this
  // avoids a lost-edit foot-gun if the window is closed without an
  // explicit save step.
  void save();

  // Second notebook page: fleetwm-bar's own bar.toml preferences (kept
  // in a separate file/struct from ThemeConfig -- see bar_config.hpp --
  // per explicit user choice not to fold bar-only settings like clock
  // format into theme.toml).
  GtkWidget* build_bar_tab();
  static void on_clock_toggle_changed(GtkCheckButton* button, gpointer user_data);
  static void on_workspace_color_set(GtkColorButton* button, gpointer user_data);
  static void on_buttons_rounded_toggled(GtkCheckButton* button, gpointer user_data);
  void save_bar();

  // Third notebook page: fleetwm-wallpaper's own wallpaper.toml (a
  // single "path" field) -- picking a file writes the path immediately,
  // fleetwm-wallpaper (running separately, autostarted by the
  // compositor) picks it up live via its own GFileMonitor watch, same
  // pattern as the Bar tab's live-reloading colors.
  GtkWidget* build_wallpaper_tab();
  // Fourth notebook page: static project info -- name, description, a
  // clickable link to the repo, and sole-developer credit. Nothing here
  // reads/writes config, so no dedicated save_*()/on_*_changed() pair.
  GtkWidget* build_about_tab();
  // Fifth notebook page: per-category default application picker, backed
  // directly by GIO's g_app_info_set_as_default_for_type() for every
  // mimetype-based category (browser, file manager, image viewer, text
  // editor, video player, PDF viewer, archive manager) -- that call
  // reads/writes ~/.config/mimeapps.list itself, the same file xdg-mime
  // and every other XDG-aware app reads, so those categories need no
  // fleetwm-side config storage. Terminal is the one exception (no XDG
  // mimetype exists for "terminal emulator"), backed instead by
  // default_apps.toml (see default_apps.hpp) which compositor/input.cpp
  // reads for its Alt+Return spawn keybind.
  GtkWidget* build_default_apps_tab();
  GtkWidget* build_mime_default_row(const char* label, const char* mime_type);
  GtkWidget* build_terminal_default_row();
  static void on_mime_default_selected(GtkCheckButton* button, gpointer user_data);
  static void on_terminal_default_selected(GtkCheckButton* button, gpointer user_data);
  static void on_choose_wallpaper_clicked(GtkButton* button, gpointer user_data);
  static void on_wallpaper_file_chosen(GObject* source, GAsyncResult* result,
                                        gpointer user_data);
  static void on_use_solid_color_toggled(GtkCheckButton* button, gpointer user_data);
  static void on_solid_color_set(GtkColorButton* button, gpointer user_data);
  void save_wallpaper();

  // Power section, appended to the main/first page ("Home") only when
  // BatterySource::battery_present() -- desktops with no battery never
  // see this at all, per explicit user request. Three linked toggle
  // buttons (not a dropdown) so each mode's icon stays visible at a
  // glance, same "reuse the workspace-button chrome" pattern as the
  // rest of the bar/settings UI.
  void build_power_section(GtkWidget* parent_box);
  void set_power_mode(PowerMode mode);
  static void on_power_mode_button_toggled(GtkToggleButton* button, gpointer user_data);
  void on_battery_reading(const BatterySource::Reading& reading);

  ThemeConfig config_;
  BarConfig bar_config_;
  WallpaperConfig wallpaper_config_;
  DefaultAppsConfig default_apps_config_;
  GtkWidget* window_ = nullptr;
  GtkWidget* accent_color_button_ = nullptr;
  GtkWidget* focus_border_color_button_ = nullptr;
  GtkWidget* pinned_border_color_button_ = nullptr;
  GtkWidget* pinned_focused_border_color_button_ = nullptr;
  GtkWidget* wallpaper_path_label_ = nullptr;
  GtkWidget* choose_image_button_ = nullptr;
  GtkWidget* solid_color_button_ = nullptr;

  GtkWidget* battery_status_label_ = nullptr;
  std::array<GtkWidget*, 3> power_mode_buttons_{};
  BatterySource battery_source_;
};

}  // namespace fleetwm::settings
