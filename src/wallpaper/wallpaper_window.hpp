#pragma once

#include <gtk/gtk.h>

#include "app_style.hpp"
#include "theme.hpp"
#include "wallpaper_config.hpp"

namespace fleetwm::wallpaper {

// The wallpaper renderer's single long-lived BACKGROUND-layer surface --
// no UI, no picker (that lives in fleetwm-settings' "Wallpaper" tab).
// Anchored to all four edges so gtk4-layer-shell/wlroots size it to
// fill the whole output (same anchor mechanism fleetwm-bar already uses
// for the top strip, just on every edge here instead of three), holding
// a single GtkPicture that fills the surface. Watches wallpaper.toml
// live via GFileMonitor (same pattern as the bar's own theme.toml/
// bar.toml watches) so picking a new image in settings updates the
// background immediately, no restart needed.
class WallpaperWindow {
 public:
  explicit WallpaperWindow(GtkApplication* app);

 private:
  static void on_activate(GtkApplication* app, gpointer user_data);
  void build(GtkApplication* app);

  void reload_image();
  void apply_theme();

  void start_config_watch();
  static void on_wallpaper_file_changed(GFileMonitor* monitor, GFile* file, GFile* other_file,
                                         GFileMonitorEvent event_type, gpointer user_data);
  static void on_theme_file_changed(GFileMonitor* monitor, GFile* file, GFile* other_file,
                                     GFileMonitorEvent event_type, gpointer user_data);

  ThemeConfig theme_config_;
  WallpaperConfig wallpaper_config_;
  GFileMonitor* wallpaper_monitor_ = nullptr;
  GFileMonitor* theme_monitor_ = nullptr;

  GtkWidget* window_ = nullptr;
  GtkWidget* picture_ = nullptr;
};

}  // namespace fleetwm::wallpaper
