#include "settings_window.hpp"

#include <cstdio>

namespace fleetwm::settings {

namespace {

GtkWidget* labeled_row(const char* label_text, GtkWidget* control) {
  GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_start(row, 4);
  gtk_widget_set_margin_end(row, 4);

  GtkWidget* label = gtk_label_new(label_text);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_widget_set_size_request(label, 120, -1);

  gtk_widget_set_hexpand(control, TRUE);

  gtk_box_append(GTK_BOX(row), label);
  gtk_box_append(GTK_BOX(row), control);
  return row;
}

}  // namespace

SettingsWindow::SettingsWindow(GtkApplication* app) {
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), this);
}

void SettingsWindow::on_activate(GtkApplication* app, gpointer user_data) {
  static_cast<SettingsWindow*>(user_data)->build(app);
}

void SettingsWindow::build(GtkApplication* app) {
  config_ = load_theme_config();

  window_ = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window_), "Fleetwm Settings");
  gtk_window_set_default_size(GTK_WINDOW(window_), 420, 280);
  gtk_window_set_decorated(GTK_WINDOW(window_), FALSE);  // compositor forces SSD; see ADR

  GtkWidget* root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(root_box, 16);
  gtk_widget_set_margin_end(root_box, 16);
  gtk_widget_set_margin_top(root_box, 16);
  gtk_widget_set_margin_bottom(root_box, 16);

  // Corner style: rounded | sharp
  const char* corner_options[] = {"Rounded", "Sharp", nullptr};
  GtkWidget* corner_dropdown = gtk_drop_down_new_from_strings(corner_options);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(corner_dropdown),
                              config_.corner_style == CornerStyle::Sharp ? 1 : 0);
  g_signal_connect(corner_dropdown, "notify::selected",
                    G_CALLBACK(on_corner_style_changed), this);
  gtk_box_append(GTK_BOX(root_box), labeled_row("Corner style", corner_dropdown));

  // Theme: dark | catppuccin | dracula | oled_black | light -- order here
  // must match the index math in on_theme_changed().
  const char* theme_options[] = {"Dark", "Catppuccin", "Dracula", "OLED Black", "Light", nullptr};
  GtkWidget* theme_dropdown = gtk_drop_down_new_from_strings(theme_options);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(theme_dropdown),
                              static_cast<guint>(config_.theme));
  g_signal_connect(theme_dropdown, "notify::selected", G_CALLBACK(on_theme_changed), this);
  gtk_box_append(GTK_BOX(root_box), labeled_row("Theme", theme_dropdown));

  // Nav mode: windows | vim
  const char* nav_options[] = {"Windows", "Vim", nullptr};
  GtkWidget* nav_dropdown = gtk_drop_down_new_from_strings(nav_options);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(nav_dropdown),
                              config_.nav_mode == NavMode::Vim ? 1 : 0);
  g_signal_connect(nav_dropdown, "notify::selected", G_CALLBACK(on_nav_mode_changed), this);
  gtk_box_append(GTK_BOX(root_box), labeled_row("Navigation", nav_dropdown));

  // Accent color: auto-extract checkbox + explicit color picker, mutually
  // exclusive per ThemeConfig::AccentColor's own auto_extract flag.
  GtkWidget* accent_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* accent_auto_check = gtk_check_button_new_with_label("Auto");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(accent_auto_check), config_.accent.auto_extract);
  g_signal_connect(accent_auto_check, "toggled", G_CALLBACK(on_accent_auto_toggled), this);

  GdkRGBA rgba{};
  gdk_rgba_parse(&rgba, config_.accent.hex.c_str());
  accent_color_button_ = gtk_color_button_new_with_rgba(&rgba);
  gtk_widget_set_sensitive(accent_color_button_, !config_.accent.auto_extract);
  g_signal_connect(accent_color_button_, "color-set", G_CALLBACK(on_accent_color_set), this);

  gtk_box_append(GTK_BOX(accent_box), accent_auto_check);
  gtk_box_append(GTK_BOX(accent_box), accent_color_button_);
  gtk_box_append(GTK_BOX(root_box), labeled_row("Accent color", accent_box));

  // Focus border thickness: how many px wide the accent-colored ring
  // drawn around the currently-focused window is. 0-10px covers
  // "off" through "chunky"; the compositor clamps nothing further, so
  // this range is just a sane UI bound, not a hard limit enforced
  // elsewhere.
  GtkWidget* thickness_spin =
      gtk_spin_button_new_with_range(0, 10, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(thickness_spin), config_.focus_border_thickness_px);
  g_signal_connect(thickness_spin, "value-changed",
                    G_CALLBACK(on_focus_border_thickness_changed), this);
  gtk_box_append(GTK_BOX(root_box), labeled_row("Focus border (px)", thickness_spin));

  gtk_window_set_child(GTK_WINDOW(window_), root_box);
  gtk_window_present(GTK_WINDOW(window_));
}

void SettingsWindow::on_corner_style_changed(GtkDropDown* dropdown, GParamSpec*,
                                              gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  guint selected = gtk_drop_down_get_selected(dropdown);
  self->config_.corner_style = selected == 1 ? CornerStyle::Sharp : CornerStyle::Rounded;
  self->save();
}

void SettingsWindow::on_theme_changed(GtkDropDown* dropdown, GParamSpec*, gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  guint selected = gtk_drop_down_get_selected(dropdown);
  // Index order matches theme_options[] in build(), which matches
  // ThemeName's declaration order in theme.hpp.
  self->config_.theme = static_cast<ThemeName>(selected);
  self->save();
}

void SettingsWindow::on_nav_mode_changed(GtkDropDown* dropdown, GParamSpec*,
                                          gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  guint selected = gtk_drop_down_get_selected(dropdown);
  self->config_.nav_mode = selected == 1 ? NavMode::Vim : NavMode::Windows;
  self->save();
}

void SettingsWindow::on_accent_auto_toggled(GtkCheckButton* button, gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  bool active = gtk_check_button_get_active(button);
  self->config_.accent.auto_extract = active;
  gtk_widget_set_sensitive(self->accent_color_button_, !active);
  self->save();
}

void SettingsWindow::on_accent_color_set(GtkColorButton* button, gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  GdkRGBA rgba{};
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
  char hex[8];
  std::snprintf(hex, sizeof(hex), "#%02x%02x%02x", static_cast<int>(rgba.red * 255),
                static_cast<int>(rgba.green * 255), static_cast<int>(rgba.blue * 255));
  self->config_.accent.hex = hex;
  self->save();
}

void SettingsWindow::on_focus_border_thickness_changed(GtkSpinButton* button,
                                                         gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  self->config_.focus_border_thickness_px = gtk_spin_button_get_value_as_int(button);
  self->save();
}

void SettingsWindow::save() {
  save_theme_config(config_);
}

}  // namespace fleetwm::settings
