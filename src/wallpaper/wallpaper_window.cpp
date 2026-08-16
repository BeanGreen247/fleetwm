#include "wallpaper_window.hpp"

#include <gtk4-layer-shell.h>

namespace fleetwm::wallpaper {

WallpaperWindow::WallpaperWindow(GtkApplication* app) {
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), this);
}

void WallpaperWindow::on_activate(GtkApplication* app, gpointer user_data) {
  static_cast<WallpaperWindow*>(user_data)->build(app);
}

void WallpaperWindow::build(GtkApplication* app) {
  theme_config_ = load_theme_config();
  wallpaper_config_ = load_wallpaper_config();

  window_ = gtk_application_window_new(app);
  gtk_window_set_decorated(GTK_WINDOW(window_), FALSE);
  // No explicit size: anchored to all four edges below, so
  // gtk4-layer-shell/wlroots size this surface to the full output --
  // unlike the bar/launcher, there's no fixed-height crash trap here
  // since nothing calls gtk_window_set_default_size() with a
  // to-content sentinel at all.
  gtk_widget_add_css_class(window_, "fleetwm-panel");  // bg_primary fallback behind the picture

  gtk_layer_init_for_window(GTK_WINDOW(window_));
  gtk_layer_set_layer(GTK_WINDOW(window_), GTK_LAYER_SHELL_LAYER_BACKGROUND);
  gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
  // No exclusive zone: a wallpaper must never reserve space the way the
  // bar does -- tiled windows should render right over it, not leave a
  // gap for it.
  gtk_layer_set_exclusive_zone(GTK_WINDOW(window_), 0);
  gtk_layer_set_keyboard_mode(GTK_WINDOW(window_), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);

  picture_ = gtk_picture_new();
  gtk_picture_set_content_fit(GTK_PICTURE(picture_), GTK_CONTENT_FIT_COVER);
  gtk_widget_set_hexpand(picture_, TRUE);
  gtk_widget_set_vexpand(picture_, TRUE);
  gtk_window_set_child(GTK_WINDOW(window_), picture_);

  apply_theme();
  reload_image();
  start_config_watch();

  gtk_window_present(GTK_WINDOW(window_));
}

void WallpaperWindow::reload_image() {
  // Always reconcile the solid-color CSS override first (applies or
  // clears it depending on use_solid_color) regardless of which branch
  // runs below -- keeps it from going stale if the user switches from
  // solid-color back to an image.
  apply_theme();

  if (wallpaper_config_.use_solid_color || wallpaper_config_.path.empty()) {
    // Either explicitly in solid-color mode, or no image has ever been
    // picked -- either way, no picture to render. The window's own
    // background (bg_primary, or the solid-color override just applied
    // above) shows through instead.
    gtk_picture_set_paintable(GTK_PICTURE(picture_), nullptr);
    return;
  }
  // gtk_picture_set_filename() (not the _new_for_filename constructor,
  // since picture_ already exists) silently clears the paintable and
  // logs a GLib warning on a missing/unreadable file rather than
  // crashing -- acceptable degrade-gracefully behavior for a path that
  // moved or was deleted after being set.
  gtk_picture_set_filename(GTK_PICTURE(picture_), wallpaper_config_.path.c_str());
}

void WallpaperWindow::apply_theme() {
  apply_app_style(theme_config_);

  // Solid-color override on top of .fleetwm-panel's own bg_primary
  // fallback -- same "static theme CSS + small dynamic per-user
  // override" pattern as the bar's accent border/workspace colors
  // (apply_theme() in bar_window.cpp).
  static GtkCssProvider* provider = nullptr;
  if (!provider) {
    provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  }

  std::string css;
  if (wallpaper_config_.use_solid_color) {
    css = "window.fleetwm-panel { background-color: " + wallpaper_config_.solid_color + "; }";
  }
  gtk_css_provider_load_from_string(provider, css.c_str());
}

void WallpaperWindow::start_config_watch() {
  GFile* wallpaper_file = g_file_new_for_path(wallpaper_user_config_path().c_str());
  wallpaper_monitor_ = g_file_monitor_file(wallpaper_file, G_FILE_MONITOR_NONE, nullptr, nullptr);
  g_object_unref(wallpaper_file);
  if (wallpaper_monitor_) {
    g_signal_connect(wallpaper_monitor_, "changed", G_CALLBACK(on_wallpaper_file_changed), this);
  }

  GFile* theme_file = g_file_new_for_path(user_config_path().c_str());
  theme_monitor_ = g_file_monitor_file(theme_file, G_FILE_MONITOR_NONE, nullptr, nullptr);
  g_object_unref(theme_file);
  if (theme_monitor_) {
    g_signal_connect(theme_monitor_, "changed", G_CALLBACK(on_theme_file_changed), this);
  }
}

void WallpaperWindow::on_wallpaper_file_changed(GFileMonitor*, GFile*, GFile*,
                                                 GFileMonitorEvent event_type,
                                                 gpointer user_data) {
  (void)event_type;
  auto* self = static_cast<WallpaperWindow*>(user_data);
  self->wallpaper_config_ = load_wallpaper_config();
  self->reload_image();
}

void WallpaperWindow::on_theme_file_changed(GFileMonitor*, GFile*, GFile*,
                                             GFileMonitorEvent event_type, gpointer user_data) {
  (void)event_type;
  auto* self = static_cast<WallpaperWindow*>(user_data);
  self->theme_config_ = load_theme_config();
  self->apply_theme();
}

}  // namespace fleetwm::wallpaper
