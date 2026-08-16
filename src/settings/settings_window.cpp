#include "settings_window.hpp"

#include <cstddef>
#include <cstdio>
#include <string>

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

// Builds a labeled row of GtkCheckButtons wired together as a mutually-
// exclusive radio group (gtk_check_button_set_group) -- explicit user
// request to replace the Theme tab's dropdowns with visible radio
// buttons per option instead of a collapsed menu. `callback` fires on
// "toggled" for every button in the group (both the newly-active one and
// whichever was previously active going inactive); callers must ignore
// calls where gtk_check_button_get_active() is false. Each button's
// option index is stashed via g_object_set_data under "radio-index" for
// the callback to read back.
GtkWidget* radio_row(const char* label_text, const char* const* options, int count, int selected,
                      GCallback callback, gpointer user_data) {
  GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_start(row, 4);
  gtk_widget_set_margin_end(row, 4);

  GtkWidget* label = gtk_label_new(label_text);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_widget_set_size_request(label, 120, -1);
  gtk_box_append(GTK_BOX(row), label);

  GtkWidget* radio_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_hexpand(radio_box, TRUE);

  GtkCheckButton* group_leader = nullptr;
  for (int i = 0; i < count; ++i) {
    GtkWidget* radio = gtk_check_button_new_with_label(options[i]);
    if (group_leader) {
      gtk_check_button_set_group(GTK_CHECK_BUTTON(radio), group_leader);
    } else {
      group_leader = GTK_CHECK_BUTTON(radio);
    }
    gtk_check_button_set_active(GTK_CHECK_BUTTON(radio), i == selected);
    g_object_set_data(G_OBJECT(radio), "radio-index", GINT_TO_POINTER(i));
    g_signal_connect(radio, "toggled", callback, user_data);
    gtk_box_append(GTK_BOX(radio_box), radio);
  }
  gtk_box_append(GTK_BOX(row), radio_box);
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
  bar_config_ = load_bar_config();
  wallpaper_config_ = load_wallpaper_config();

  window_ = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window_), "Fleetwm Settings");
  gtk_window_set_default_size(GTK_WINDOW(window_), 420, 320);
  gtk_window_set_decorated(GTK_WINDOW(window_), FALSE);  // compositor forces SSD; see ADR
  // Same dark panel background + corner_style-driven radius as
  // fleetwm-launcher (themes/base.css's "window.fleetwm-panel" rule). No
  // border here -- matches i3/dwm, where the only border concept is the
  // compositor's own focused-window border, not an outline around every
  // themed app window.
  gtk_widget_add_css_class(window_, "fleetwm-panel");

  GtkWidget* root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(root_box, 16);
  gtk_widget_set_margin_end(root_box, 16);
  gtk_widget_set_margin_top(root_box, 16);
  gtk_widget_set_margin_bottom(root_box, 16);

  // Theme: dark | catppuccin | dracula | oled_black | light -- order here
  // must match ThemeName's declaration order (theme.hpp), which the
  // callback casts the radio index straight to.
  const char* theme_options[] = {"Dark", "Catppuccin", "Dracula", "OLED Black", "Light"};
  gtk_box_append(GTK_BOX(root_box),
                 radio_row("Theme", theme_options, 5, static_cast<int>(config_.theme),
                           G_CALLBACK(on_theme_changed), this));

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

  // Focus border thickness: how many px wide the ring drawn around the
  // currently-focused window is. 0-10px covers "off" through "chunky";
  // the compositor clamps nothing further, so this range is just a sane
  // UI bound, not a hard limit enforced elsewhere.
  GtkWidget* thickness_spin =
      gtk_spin_button_new_with_range(0, 10, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(thickness_spin), config_.focus_border_thickness_px);
  g_signal_connect(thickness_spin, "value-changed",
                    G_CALLBACK(on_focus_border_thickness_changed), this);
  gtk_box_append(GTK_BOX(root_box), labeled_row("Focus border (px)", thickness_spin));

  // Focus border color: deliberately a separate setting from Accent
  // color above -- accent drives UI chrome (bar, launcher, settings,
  // selection highlights) everywhere, and reusing it for the
  // focused-window border too made it hard to tell which window has
  // focus when everything on screen already reads as the same color.
  GdkRGBA focus_rgba{};
  gdk_rgba_parse(&focus_rgba, config_.focus_border_color.c_str());
  focus_border_color_button_ = gtk_color_button_new_with_rgba(&focus_rgba);
  g_signal_connect(focus_border_color_button_, "color-set",
                    G_CALLBACK(on_focus_border_color_set), this);
  gtk_box_append(GTK_BOX(root_box),
                  labeled_row("Focus border color", focus_border_color_button_));

  // Gap between tiled windows (master/stack split, and between stacked
  // windows). Does not add spacing against the bar or screen edges --
  // that's a separate, fixed exclusive-zone reservation.
  GtkWidget* gap_spin = gtk_spin_button_new_with_range(0, 64, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(gap_spin), config_.gap_px);
  g_signal_connect(gap_spin, "value-changed", G_CALLBACK(on_gap_changed), this);
  gtk_box_append(GTK_BOX(root_box), labeled_row("Window gap (px)", gap_spin));

  build_power_section(root_box);

  GtkWidget* notebook = gtk_notebook_new();
  gtk_widget_add_css_class(notebook, "fleetwm-settings-notebook");
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), root_box, gtk_label_new("Theme"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_bar_tab(), gtk_label_new("Bar"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_wallpaper_tab(),
                            gtk_label_new("Wallpaper"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_about_tab(), gtk_label_new("About"));

  gtk_window_set_child(GTK_WINDOW(window_), notebook);
  apply_theme();
  gtk_window_present(GTK_WINDOW(window_));
}

void SettingsWindow::apply_theme() {
  // Background, text colors, and corner_style-driven radius (window.
  // fleetwm-panel in themes/base.css + themes/corners-*.css) all come
  // from the installed theme CSS stack -- no border, no per-process
  // dynamic override needed here. Still called from save() so switching
  // theme/corner_style live-restyles this window immediately.
  apply_app_style(config_);
}

GtkWidget* SettingsWindow::build_bar_tab() {
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(box, 16);
  gtk_widget_set_margin_end(box, 16);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);

  GtkWidget* heading = gtk_label_new("Clock");
  gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
  PangoAttrList* attrs = pango_attr_list_new();
  pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
  gtk_label_set_attributes(GTK_LABEL(heading), attrs);
  pango_attr_list_unref(attrs);
  gtk_box_append(GTK_BOX(box), heading);

  struct ToggleSpec {
    const char* label;
    int field_offset;  // offsetof(ClockFormat, <field>)
  };
  static const ToggleSpec toggles[] = {
      {"Show seconds", offsetof(ClockFormat, show_seconds)},
      {"Show date", offsetof(ClockFormat, show_date)},
      {"Show year", offsetof(ClockFormat, show_year)},
      {"Show month", offsetof(ClockFormat, show_month)},
      {"Show day", offsetof(ClockFormat, show_day)},
  };
  for (const ToggleSpec& spec : toggles) {
    bool* field = reinterpret_cast<bool*>(reinterpret_cast<char*>(&bar_config_.clock) +
                                           spec.field_offset);
    GtkWidget* check = gtk_check_button_new_with_label(spec.label);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check), *field);
    g_object_set_data(G_OBJECT(check), "clock-field-offset", GINT_TO_POINTER(spec.field_offset));
    g_signal_connect(check, "toggled", G_CALLBACK(on_clock_toggle_changed), this);
    gtk_box_append(GTK_BOX(box), check);
  }

  GtkWidget* ws_heading = gtk_label_new("Workspace colors");
  gtk_label_set_xalign(GTK_LABEL(ws_heading), 0.0f);
  gtk_widget_set_margin_top(ws_heading, 12);
  PangoAttrList* ws_attrs = pango_attr_list_new();
  pango_attr_list_insert(ws_attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
  gtk_label_set_attributes(GTK_LABEL(ws_heading), ws_attrs);
  pango_attr_list_unref(ws_attrs);
  gtk_box_append(GTK_BOX(box), ws_heading);

  struct ColorSpec {
    const char* label;
    int field_offset;  // offsetof(WorkspaceColors, <field>)
  };
  static const ColorSpec colors[] = {
      {"Inactive background", offsetof(WorkspaceColors, inactive_bg)},
      {"Inactive text", offsetof(WorkspaceColors, inactive_fg)},
      {"Active background", offsetof(WorkspaceColors, active_bg)},
      {"Active text", offsetof(WorkspaceColors, active_fg)},
  };
  for (const ColorSpec& spec : colors) {
    auto* field = reinterpret_cast<std::string*>(
        reinterpret_cast<char*>(&bar_config_.workspace_colors) + spec.field_offset);
    GdkRGBA rgba{};
    gdk_rgba_parse(&rgba, field->c_str());
    GtkWidget* button = gtk_color_button_new_with_rgba(&rgba);
    g_object_set_data(G_OBJECT(button), "ws-color-field-offset", GINT_TO_POINTER(spec.field_offset));
    g_signal_connect(button, "color-set", G_CALLBACK(on_workspace_color_set), this);
    gtk_box_append(GTK_BOX(box), labeled_row(spec.label, button));
  }

  // Independent of theme.toml's window/launcher/panel corner_style --
  // explicit user request to pick the workspace/power button shape on
  // its own.
  GtkWidget* buttons_rounded_check = gtk_check_button_new_with_label("Rounded workspace buttons");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(buttons_rounded_check),
                               bar_config_.workspace_colors.buttons_rounded);
  gtk_widget_set_margin_top(buttons_rounded_check, 8);
  g_signal_connect(buttons_rounded_check, "toggled", G_CALLBACK(on_buttons_rounded_toggled), this);
  gtk_box_append(GTK_BOX(box), buttons_rounded_check);

  return box;
}

void SettingsWindow::on_buttons_rounded_toggled(GtkCheckButton* button, gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  self->bar_config_.workspace_colors.buttons_rounded = gtk_check_button_get_active(button);
  self->save_bar();
}

void SettingsWindow::on_clock_toggle_changed(GtkCheckButton* button, gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  bool active = gtk_check_button_get_active(button);
  int offset = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "clock-field-offset"));
  bool* field =
      reinterpret_cast<bool*>(reinterpret_cast<char*>(&self->bar_config_.clock) + offset);
  *field = active;
  self->save_bar();
}

void SettingsWindow::on_workspace_color_set(GtkColorButton* button, gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  GdkRGBA rgba{};
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
  char hex[8];
  std::snprintf(hex, sizeof(hex), "#%02x%02x%02x", static_cast<int>(rgba.red * 255),
                static_cast<int>(rgba.green * 255), static_cast<int>(rgba.blue * 255));

  int offset = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "ws-color-field-offset"));
  auto* field = reinterpret_cast<std::string*>(
      reinterpret_cast<char*>(&self->bar_config_.workspace_colors) + offset);
  *field = hex;
  self->save_bar();
}

void SettingsWindow::save_bar() {
  save_bar_config(bar_config_);
}

GtkWidget* SettingsWindow::build_wallpaper_tab() {
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(box, 16);
  gtk_widget_set_margin_end(box, 16);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);

  GtkWidget* heading = gtk_label_new("Wallpaper");
  gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
  PangoAttrList* attrs = pango_attr_list_new();
  pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
  gtk_label_set_attributes(GTK_LABEL(heading), attrs);
  pango_attr_list_unref(attrs);
  gtk_box_append(GTK_BOX(box), heading);

  wallpaper_path_label_ = gtk_label_new(
      wallpaper_config_.path.empty() ? "No wallpaper set" : wallpaper_config_.path.c_str());
  gtk_label_set_xalign(GTK_LABEL(wallpaper_path_label_), 0.0f);
  gtk_label_set_ellipsize(GTK_LABEL(wallpaper_path_label_), PANGO_ELLIPSIZE_MIDDLE);
  gtk_widget_add_css_class(wallpaper_path_label_, "fleetwm-stat");
  gtk_box_append(GTK_BOX(box), wallpaper_path_label_);

  GtkWidget* choose_button = gtk_button_new_with_label("Choose Image...");
  g_signal_connect(choose_button, "clicked", G_CALLBACK(on_choose_wallpaper_clicked), this);
  choose_image_button_ = choose_button;
  gtk_widget_set_sensitive(choose_button, !wallpaper_config_.use_solid_color);
  gtk_box_append(GTK_BOX(box), choose_button);

  GtkWidget* solid_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_top(solid_row, 8);

  GtkWidget* solid_check = gtk_check_button_new_with_label("Use solid color");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(solid_check), wallpaper_config_.use_solid_color);
  g_signal_connect(solid_check, "toggled", G_CALLBACK(on_use_solid_color_toggled), this);

  GdkRGBA rgba{};
  gdk_rgba_parse(&rgba, wallpaper_config_.solid_color.c_str());
  solid_color_button_ = gtk_color_button_new_with_rgba(&rgba);
  gtk_widget_set_sensitive(solid_color_button_, wallpaper_config_.use_solid_color);
  g_signal_connect(solid_color_button_, "color-set", G_CALLBACK(on_solid_color_set), this);

  gtk_box_append(GTK_BOX(solid_row), solid_check);
  gtk_box_append(GTK_BOX(solid_row), solid_color_button_);
  gtk_box_append(GTK_BOX(box), solid_row);

  return box;
}

void SettingsWindow::on_choose_wallpaper_clicked(GtkButton*, gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);

  GtkFileDialog* dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Choose Wallpaper");

  GtkFileFilter* filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "Images");
  gtk_file_filter_add_pixbuf_formats(filter);
  GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
  g_list_store_append(filters, filter);
  gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
  gtk_file_dialog_set_default_filter(dialog, filter);
  g_object_unref(filter);
  g_object_unref(filters);

  gtk_file_dialog_open(dialog, GTK_WINDOW(self->window_), nullptr, on_wallpaper_file_chosen,
                        self);
  g_object_unref(dialog);
}

void SettingsWindow::on_wallpaper_file_chosen(GObject* source, GAsyncResult* result,
                                               gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  GError* error = nullptr;
  GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, &error);
  if (!file) {
    // User cancelled the dialog, or a real error -- either way there's
    // nothing to set. GTK_DIALOG_ERROR_DISMISSED (plain cancel) isn't
    // worth logging; only note anything else.
    if (error && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
      std::fprintf(stderr, "fleetwm-settings: wallpaper file dialog failed: %s\n",
                   error->message);
    }
    g_clear_error(&error);
    return;
  }

  char* path = g_file_get_path(file);
  g_object_unref(file);
  if (!path) {
    return;  // e.g. a non-local GVfs URI with no local path -- out of scope
  }
  self->wallpaper_config_.path = path;
  g_free(path);

  gtk_label_set_text(GTK_LABEL(self->wallpaper_path_label_), self->wallpaper_config_.path.c_str());
  self->save_wallpaper();
}

void SettingsWindow::on_use_solid_color_toggled(GtkCheckButton* button, gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  bool active = gtk_check_button_get_active(button);
  self->wallpaper_config_.use_solid_color = active;
  gtk_widget_set_sensitive(self->solid_color_button_, active);
  gtk_widget_set_sensitive(self->choose_image_button_, !active);
  self->save_wallpaper();
}

void SettingsWindow::on_solid_color_set(GtkColorButton* button, gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  GdkRGBA rgba{};
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
  char hex[8];
  std::snprintf(hex, sizeof(hex), "#%02x%02x%02x", static_cast<int>(rgba.red * 255),
                static_cast<int>(rgba.green * 255), static_cast<int>(rgba.blue * 255));
  self->wallpaper_config_.solid_color = hex;
  self->save_wallpaper();
}

void SettingsWindow::save_wallpaper() {
  save_wallpaper_config(wallpaper_config_);
}

GtkWidget* SettingsWindow::build_about_tab() {
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(box, 16);
  gtk_widget_set_margin_end(box, 16);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);

  GtkWidget* heading = gtk_label_new("fleetwm");
  gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
  PangoAttrList* attrs = pango_attr_list_new();
  pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
  pango_attr_list_insert(attrs, pango_attr_scale_new(1.3));
  gtk_label_set_attributes(GTK_LABEL(heading), attrs);
  pango_attr_list_unref(attrs);
  gtk_box_append(GTK_BOX(box), heading);

  GtkWidget* description = gtk_label_new(
      "A custom wlroots-based Wayland compositor and desktop shell "
      "(bar, settings, launcher, wallpaper, greeter).");
  gtk_label_set_xalign(GTK_LABEL(description), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(description), TRUE);
  gtk_widget_add_css_class(description, "fleetwm-stat");
  gtk_box_append(GTK_BOX(box), description);

  GtkWidget* link = gtk_link_button_new_with_label(
      "https://github.com/BeanGreen247/fleetwm", "github.com/BeanGreen247/fleetwm");
  gtk_widget_set_halign(link, GTK_ALIGN_START);
  gtk_widget_set_margin_top(link, 4);
  gtk_box_append(GTK_BOX(box), link);

  GtkWidget* developer = gtk_label_new("Sole developer: BeanGreen247");
  gtk_label_set_xalign(GTK_LABEL(developer), 0.0f);
  gtk_widget_set_margin_top(developer, 8);
  gtk_box_append(GTK_BOX(box), developer);

  GtkWidget* license = gtk_label_new("License: MIT");
  gtk_label_set_xalign(GTK_LABEL(license), 0.0f);
  gtk_widget_add_css_class(license, "fleetwm-stat");
  gtk_box_append(GTK_BOX(box), license);

  return box;
}

void SettingsWindow::on_theme_changed(GtkCheckButton* button, gpointer user_data) {
  if (!gtk_check_button_get_active(button)) {
    return;
  }
  auto* self = static_cast<SettingsWindow*>(user_data);
  int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "radio-index"));
  // Index order matches theme_options[] in build(), which matches
  // ThemeName's declaration order in theme.hpp.
  self->config_.theme = static_cast<ThemeName>(index);
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

void SettingsWindow::on_gap_changed(GtkSpinButton* button, gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  self->config_.gap_px = gtk_spin_button_get_value_as_int(button);
  self->save();
}

void SettingsWindow::on_focus_border_color_set(GtkColorButton* button, gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  GdkRGBA rgba{};
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
  char hex[8];
  std::snprintf(hex, sizeof(hex), "#%02x%02x%02x", static_cast<int>(rgba.red * 255),
                static_cast<int>(rgba.green * 255), static_cast<int>(rgba.blue * 255));
  self->config_.focus_border_color = hex;
  self->save();
}

void SettingsWindow::build_power_section(GtkWidget* parent_box) {
  // Only surfaced on machines with a battery -- a desktop with none has
  // nothing meaningful for "Performance"/"Battery Saver" to trade off,
  // per explicit user request to gate this on battery/laptop detection.
  if (!BatterySource::battery_present()) {
    return;
  }

  GtkWidget* heading = gtk_label_new("Power");
  gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
  gtk_widget_set_margin_top(heading, 4);
  PangoAttrList* attrs = pango_attr_list_new();
  pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
  gtk_label_set_attributes(GTK_LABEL(heading), attrs);
  pango_attr_list_unref(attrs);
  gtk_box_append(GTK_BOX(parent_box), heading);

  battery_status_label_ = gtk_label_new("Battery: --");
  gtk_label_set_xalign(GTK_LABEL(battery_status_label_), 0.0f);
  gtk_widget_add_css_class(battery_status_label_, "fleetwm-stat");
  gtk_box_append(GTK_BOX(parent_box), battery_status_label_);
  battery_source_.start([this](const BatterySource::Reading& reading) { on_battery_reading(reading); });

  struct ModeSpec {
    const char* label;
    const char* icon_name;
    PowerMode mode;
  };
  static const ModeSpec specs[] = {
      {"Normal", "power-profile-balanced-symbolic", PowerMode::Normal},
      {"Performance", "power-profile-performance-symbolic", PowerMode::Performance},
      {"Battery Saver", "power-profile-power-saver-symbolic", PowerMode::BatterySaver},
  };

  GtkWidget* mode_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class(mode_box, "linked");
  gtk_widget_set_margin_top(mode_box, 4);

  GtkToggleButton* group_leader = nullptr;
  for (size_t i = 0; i < sizeof(specs) / sizeof(specs[0]); ++i) {
    const ModeSpec& spec = specs[i];
    GtkWidget* button = gtk_toggle_button_new();
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* icon = gtk_image_new_from_icon_name(spec.icon_name);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
    gtk_box_append(GTK_BOX(content), icon);
    gtk_box_append(GTK_BOX(content), gtk_label_new(spec.label));
    gtk_button_set_child(GTK_BUTTON(button), content);
    gtk_widget_set_hexpand(button, TRUE);

    if (group_leader) {
      gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(button), group_leader);
    } else {
      group_leader = GTK_TOGGLE_BUTTON(button);
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), bar_config_.power_mode == spec.mode);
    g_object_set_data(G_OBJECT(button), "power-mode", GINT_TO_POINTER(static_cast<int>(spec.mode)));
    g_signal_connect(button, "toggled", G_CALLBACK(on_power_mode_button_toggled), this);

    power_mode_buttons_[i] = button;
    gtk_box_append(GTK_BOX(mode_box), button);
  }
  gtk_box_append(GTK_BOX(parent_box), labeled_row("Power mode", mode_box));
}

void SettingsWindow::on_power_mode_button_toggled(GtkToggleButton* button, gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  if (!gtk_toggle_button_get_active(button)) {
    return;
  }
  auto mode = static_cast<PowerMode>(
      GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "power-mode")));
  self->set_power_mode(mode);
}

void SettingsWindow::set_power_mode(PowerMode mode) {
  bar_config_.power_mode = mode;
  save_bar_config(bar_config_);  // fleetwm-bar picks this up live via its own GFileMonitor watch

  // Applying the system power profile is a fire-and-forget shell out to
  // powerprofilesctl (power-profiles-daemon's own CLI) -- same
  // g_spawn_async, no-blocking-the-UI pattern as the bar's power menu
  // actions (logout/reboot/shutdown).
  char* argv[] = {const_cast<char*>("powerprofilesctl"), const_cast<char*>("set"),
                   const_cast<char*>(power_mode_to_profiles_daemon_name(mode).c_str()), nullptr};
  GError* error = nullptr;
  if (!g_spawn_async(nullptr, argv, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr,
                      &error)) {
    std::fprintf(stderr, "fleetwm-settings: powerprofilesctl set failed: %s\n",
                 error != nullptr ? error->message : "unknown error");
    g_clear_error(&error);
  }
}

void SettingsWindow::on_battery_reading(const BatterySource::Reading& reading) {
  if (!reading.available) {
    gtk_label_set_text(GTK_LABEL(battery_status_label_), "Battery: N/A");
    return;
  }
  std::string text = "Battery: " + std::to_string(reading.percent) + "%";
  text += reading.charging ? " (charging)" : " (on battery)";
  if (reading.hours_remaining >= 0.0) {
    int total_minutes = static_cast<int>(reading.hours_remaining * 60.0 + 0.5);
    text += " -- " + std::to_string(total_minutes / 60) + "h " +
            std::to_string(total_minutes % 60) + "m " +
            (reading.charging ? "until full" : "remaining");
  }
  gtk_label_set_text(GTK_LABEL(battery_status_label_), text.c_str());
}

void SettingsWindow::save() {
  save_theme_config(config_);
  // Live-preview this window's own theme change immediately -- every
  // handler that mutates config_ (theme/accent)
  // already calls save() right after, so this is the one call site that
  // covers all of them rather than duplicating the apply_theme() call
  // at each handler.
  apply_theme();
}

}  // namespace fleetwm::settings
