#include "settings_window.hpp"

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
  default_apps_config_ = load_default_apps_config();

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

  // Pinned-window (always-on-top) border: previously hardcoded blue/green
  // constants in compositor/view.cpp, now themeable the same way the
  // plain focus border above is. One shared thickness for both the
  // plain-pinned and pinned+focused states (matches the previous
  // hardcoded behavior, where both constants already happened to be the
  // same value) -- only the color differs between them, so
  // pinned+focused still reads as visually distinct from merely pinned.
  GtkWidget* pinned_thickness_spin = gtk_spin_button_new_with_range(0, 10, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(pinned_thickness_spin),
                             config_.pinned_border_thickness_px);
  g_signal_connect(pinned_thickness_spin, "value-changed",
                    G_CALLBACK(on_pinned_border_thickness_changed), this);
  gtk_box_append(GTK_BOX(root_box), labeled_row("Pinned border (px)", pinned_thickness_spin));

  GdkRGBA pinned_rgba{};
  gdk_rgba_parse(&pinned_rgba, config_.pinned_border_color.c_str());
  pinned_border_color_button_ = gtk_color_button_new_with_rgba(&pinned_rgba);
  g_signal_connect(pinned_border_color_button_, "color-set",
                    G_CALLBACK(on_pinned_border_color_set), this);
  gtk_box_append(GTK_BOX(root_box),
                  labeled_row("Pinned border color", pinned_border_color_button_));

  GdkRGBA pinned_focused_rgba{};
  gdk_rgba_parse(&pinned_focused_rgba, config_.pinned_focused_border_color.c_str());
  pinned_focused_border_color_button_ = gtk_color_button_new_with_rgba(&pinned_focused_rgba);
  g_signal_connect(pinned_focused_border_color_button_, "color-set",
                    G_CALLBACK(on_pinned_focused_border_color_set), this);
  gtk_box_append(
      GTK_BOX(root_box),
      labeled_row("Pinned+focused border color", pinned_focused_border_color_button_));

  build_power_section(root_box);

  GtkWidget* notebook = gtk_notebook_new();
  gtk_widget_add_css_class(notebook, "fleetwm-settings-notebook");
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), root_box, gtk_label_new("Theme"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_bar_tab(), gtk_label_new("Bar"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_wallpaper_tab(),
                            gtk_label_new("Wallpaper"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_default_apps_tab(),
                            gtk_label_new("Default Apps"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_audio_tab(), gtk_label_new("Audio"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_performance_tab(),
                            gtk_label_new("Performance"));
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

GtkWidget* SettingsWindow::build_performance_tab() {
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(box, 16);
  gtk_widget_set_margin_end(box, 16);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);

  // Adaptive render throttling: Synced (default, vsync-paced) vs Custom
  // (an explicit FPS cap for ordinary desktop content). Deliberately no
  // "Uncapped" option here -- real tearing is applied automatically to
  // fullscreen apps/games only, not a user-selectable mode (see
  // RenderMode's own doc comment in theme.hpp).
  const char* render_mode_options[] = {"Synced (Unlocked)", "Custom (FPS Cap)"};
  gtk_box_append(
      GTK_BOX(box),
      radio_row("Render mode", render_mode_options, 2, static_cast<int>(config_.render_mode),
                G_CALLBACK(on_render_mode_changed), this));

  custom_fps_lock_spin_ = gtk_spin_button_new_with_range(24, 5000, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(custom_fps_lock_spin_), config_.custom_fps_lock);
  gtk_widget_set_sensitive(custom_fps_lock_spin_, config_.render_mode == RenderMode::Custom);
  g_signal_connect(custom_fps_lock_spin_, "value-changed",
                    G_CALLBACK(on_custom_fps_lock_changed), this);
  gtk_box_append(GTK_BOX(box), labeled_row("Custom FPS lock", custom_fps_lock_spin_));

  // Pre-sets Server::debug_overlay_enabled_ on compositor startup instead
  // of requiring the Alt+Shift+I keybind every session -- same overlay
  // (frame-time bar graph + FPS/RAM/CPU-MHz text), just a persisted
  // startup default. Applied by the compositor at Server::init() time
  // (server.cpp); this checkbox itself only ever writes theme.toml, same
  // as every other Settings control.
  GtkWidget* overlay_check = gtk_check_button_new_with_label("Show performance overlay on startup");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(overlay_check),
                               config_.show_debug_overlay_on_startup);
  g_signal_connect(overlay_check, "toggled", G_CALLBACK(on_show_debug_overlay_toggled), this);
  gtk_box_append(GTK_BOX(box), overlay_check);

  return box;
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

namespace {

// Frees a std::vector<GAppInfo*> and every GAppInfo* it owns -- passed as
// the GDestroyNotify to g_object_set_data_full() on each Default Apps
// row, so the vector's lifetime tracks its row widget's own (freed
// automatically when GTK tears down the notebook on window close).
void free_app_info_vector(gpointer data) {
  auto* apps = static_cast<std::vector<GAppInfo*>*>(data);
  for (GAppInfo* info : *apps) {
    g_object_unref(info);
  }
  delete apps;
}

// True if `info`'s .desktop Categories field lists TerminalEmulator --
// same category vocabulary the launcher's AppIndex reads (app_index.cpp),
// but checked for membership here rather than just taken as the first
// match, since a terminal's categories list often also includes System
// or Utility ahead of TerminalEmulator.
bool is_terminal_app(GDesktopAppInfo* info) {
  const char* categories = g_desktop_app_info_get_categories(info);
  if (categories == nullptr) {
    return false;
  }
  std::string csv = categories;
  std::string needle = "TerminalEmulator";
  size_t pos = 0;
  while ((pos = csv.find(needle, pos)) != std::string::npos) {
    bool start_ok = pos == 0 || csv[pos - 1] == ';';
    size_t end = pos + needle.size();
    bool end_ok = end == csv.size() || csv[end] == ';';
    if (start_ok && end_ok) {
      return true;
    }
    pos = end;
  }
  return false;
}

}  // namespace

GtkWidget* SettingsWindow::build_default_apps_tab() {
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(box, 16);
  gtk_widget_set_margin_end(box, 16);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);

  GtkWidget* heading = gtk_label_new("Default Applications");
  gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
  PangoAttrList* attrs = pango_attr_list_new();
  pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
  gtk_label_set_attributes(GTK_LABEL(heading), attrs);
  pango_attr_list_unref(attrs);
  gtk_box_append(GTK_BOX(box), heading);

  // mime_type is null-terminated C strings understood by GIO's
  // g_app_info_get_all_for_type()/set_as_default_for_type() -- picking
  // one representative type per category (e.g. image/png rather than
  // every image/* subtype) matches how most desktop environments' own
  // "Default Applications" panels scope these categories.
  struct MimeCategory {
    const char* label;
    const char* mime_type;
  };
  static const MimeCategory categories[] = {
      {"Web Browser", "x-scheme-handler/https"},
      {"File Manager", "inode/directory"},
      {"Image Viewer", "image/png"},
      {"Text Editor", "text/plain"},
      {"Video Player", "video/mp4"},
      {"PDF Viewer", "application/pdf"},
      {"Archive Manager", "application/zip"},
  };
  for (const MimeCategory& category : categories) {
    gtk_box_append(GTK_BOX(box), build_mime_default_row(category.label, category.mime_type));
  }

  // Terminal has no XDG mimetype of its own -- see default_apps.hpp --
  // so it's the one row here backed by fleetwm's own config instead of
  // mimeapps.list.
  gtk_box_append(GTK_BOX(box), build_terminal_default_row());

  return box;
}

namespace {

// Builds the horizontal "label | radio radio radio" row shape shared by
// every Default Apps category, including the empty case (no candidate
// apps found -- a bare dim label instead of an empty radio group, rather
// than showing a category with nothing selectable in it). `apps` and
// `extra_data`/`extra_key` (mime type string, or nullptr for the
// Terminal row which needs none) are attached to the *row* itself
// (not the individual radio buttons) so every button's toggled callback
// can walk up two parents (button -> radio_box -> row) to reach them --
// same "hang shared state off a stable ancestor" approach as
// build_power_section's mode buttons.
GtkWidget* app_radio_row(const char* label, std::vector<GAppInfo*>* apps, int selected,
                          GCallback toggled_callback, gpointer user_data, const char* mime_type) {
  GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_start(row, 4);
  gtk_widget_set_margin_end(row, 4);

  GtkWidget* row_label = gtk_label_new(label);
  gtk_label_set_xalign(GTK_LABEL(row_label), 0.0f);
  gtk_widget_set_size_request(row_label, 120, -1);
  gtk_box_append(GTK_BOX(row), row_label);

  if (apps->empty()) {
    GtkWidget* none_label = gtk_label_new("No apps found");
    gtk_widget_add_css_class(none_label, "fleetwm-stat");
    gtk_widget_set_hexpand(none_label, TRUE);
    gtk_box_append(GTK_BOX(row), none_label);
    free_app_info_vector(apps);
    return row;
  }

  GtkWidget* radio_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_hexpand(radio_box, TRUE);

  GtkCheckButton* group_leader = nullptr;
  for (size_t i = 0; i < apps->size(); ++i) {
    const char* name = g_app_info_get_display_name((*apps)[i]);
    GtkWidget* radio = gtk_check_button_new_with_label(name != nullptr ? name : "(unnamed)");
    if (group_leader != nullptr) {
      gtk_check_button_set_group(GTK_CHECK_BUTTON(radio), group_leader);
    } else {
      group_leader = GTK_CHECK_BUTTON(radio);
    }
    gtk_check_button_set_active(GTK_CHECK_BUTTON(radio), static_cast<int>(i) == selected);
    g_object_set_data(G_OBJECT(radio), "radio-index", GINT_TO_POINTER(static_cast<int>(i)));
    g_signal_connect(radio, "toggled", toggled_callback, user_data);
    gtk_box_append(GTK_BOX(radio_box), radio);
  }
  gtk_box_append(GTK_BOX(row), radio_box);

  g_object_set_data_full(G_OBJECT(row), "fleetwm-apps", apps, free_app_info_vector);
  if (mime_type != nullptr) {
    g_object_set_data_full(G_OBJECT(row), "fleetwm-mime", g_strdup(mime_type), g_free);
  }
  return row;
}

}  // namespace

GtkWidget* SettingsWindow::build_mime_default_row(const char* label, const char* mime_type) {
  auto* apps = new std::vector<GAppInfo*>();
  GList* infos = g_app_info_get_all_for_type(mime_type);
  for (GList* l = infos; l != nullptr; l = l->next) {
    apps->push_back(static_cast<GAppInfo*>(l->data));  // ownership transferred from the GList
  }
  g_list_free(infos);

  int selected = -1;
  GAppInfo* current_default = g_app_info_get_default_for_type(mime_type, FALSE);
  if (current_default != nullptr) {
    for (size_t i = 0; i < apps->size(); ++i) {
      if (g_app_info_equal((*apps)[i], current_default)) {
        selected = static_cast<int>(i);
        break;
      }
    }
    g_object_unref(current_default);
  }

  return app_radio_row(label, apps, selected, G_CALLBACK(on_mime_default_selected), this,
                        mime_type);
}

void SettingsWindow::on_mime_default_selected(GtkCheckButton* button, gpointer) {
  if (!gtk_check_button_get_active(button)) {
    return;
  }
  int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "radio-index"));

  GtkWidget* radio_box = gtk_widget_get_parent(GTK_WIDGET(button));
  GtkWidget* row = gtk_widget_get_parent(radio_box);
  auto* apps = static_cast<std::vector<GAppInfo*>*>(g_object_get_data(G_OBJECT(row), "fleetwm-apps"));
  const auto* mime_type = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "fleetwm-mime"));
  if (apps == nullptr || mime_type == nullptr || index < 0 ||
      static_cast<size_t>(index) >= apps->size()) {
    return;
  }

  GError* error = nullptr;
  if (!g_app_info_set_as_default_for_type((*apps)[index], mime_type, &error)) {
    std::fprintf(stderr, "fleetwm-settings: failed to set default app for %s: %s\n", mime_type,
                 error != nullptr ? error->message : "unknown error");
    g_clear_error(&error);
  }
}

GtkWidget* SettingsWindow::build_terminal_default_row() {
  auto* apps = new std::vector<GAppInfo*>();
  GList* infos = g_app_info_get_all();
  for (GList* l = infos; l != nullptr; l = l->next) {
    auto* info = static_cast<GAppInfo*>(l->data);
    if (G_IS_DESKTOP_APP_INFO(info) && g_app_info_should_show(info) &&
        is_terminal_app(G_DESKTOP_APP_INFO(info))) {
      apps->push_back(info);  // ownership transferred from the GList
    } else {
      g_object_unref(info);
    }
  }
  g_list_free(infos);

  // Two-pass match: prefer an exact commandline match (e.g. distinguishes
  // foot.desktop's "foot" from foot-server.desktop's "foot --server" --
  // both report the same bare executable, so matching on executable alone
  // picks whichever entry happens to sort last) and only fall back to a
  // bare-executable match if nothing matched exactly, so a
  // terminal_command written before this fix (or by hand) still resolves
  // to *some* selection rather than none.
  int exact_match = -1;
  int executable_match = -1;
  for (size_t i = 0; i < apps->size(); ++i) {
    const char* commandline = g_app_info_get_commandline((*apps)[i]);
    if (commandline != nullptr && default_apps_config_.terminal_command == commandline) {
      exact_match = static_cast<int>(i);
    }
    const char* executable = g_app_info_get_executable((*apps)[i]);
    if (executable != nullptr && default_apps_config_.terminal_command == executable) {
      executable_match = static_cast<int>(i);
    }
  }
  int selected = exact_match >= 0 ? exact_match : executable_match;

  return app_radio_row("Terminal", apps, selected, G_CALLBACK(on_terminal_default_selected), this,
                        nullptr);
}

void SettingsWindow::on_terminal_default_selected(GtkCheckButton* button, gpointer user_data) {
  if (!gtk_check_button_get_active(button)) {
    return;
  }
  int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "radio-index"));

  GtkWidget* radio_box = gtk_widget_get_parent(GTK_WIDGET(button));
  GtkWidget* row = gtk_widget_get_parent(radio_box);
  auto* apps = static_cast<std::vector<GAppInfo*>*>(g_object_get_data(G_OBJECT(row), "fleetwm-apps"));
  if (apps == nullptr || index < 0 || static_cast<size_t>(index) >= apps->size()) {
    return;
  }
  const char* executable = g_app_info_get_executable((*apps)[index]);
  if (executable == nullptr) {
    return;
  }

  auto* self = static_cast<SettingsWindow*>(user_data);
  self->default_apps_config_.terminal_command = executable;
  save_default_apps_config(self->default_apps_config_);
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

void SettingsWindow::on_render_mode_changed(GtkCheckButton* button, gpointer user_data) {
  if (!gtk_check_button_get_active(button)) {
    return;
  }
  auto* self = static_cast<SettingsWindow*>(user_data);
  int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "radio-index"));
  // Index order matches render_mode_options[] in build(), which matches
  // RenderMode's declaration order in theme.hpp.
  self->config_.render_mode = static_cast<RenderMode>(index);
  gtk_widget_set_sensitive(self->custom_fps_lock_spin_,
                            self->config_.render_mode == RenderMode::Custom);
  self->save();
}

void SettingsWindow::on_custom_fps_lock_changed(GtkSpinButton* button, gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  self->config_.custom_fps_lock = gtk_spin_button_get_value_as_int(button);
  self->save();
}

void SettingsWindow::on_show_debug_overlay_toggled(GtkCheckButton* button, gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  self->config_.show_debug_overlay_on_startup = gtk_check_button_get_active(button);
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

void SettingsWindow::on_pinned_border_thickness_changed(GtkSpinButton* button,
                                                          gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  self->config_.pinned_border_thickness_px = gtk_spin_button_get_value_as_int(button);
  self->save();
}

void SettingsWindow::on_pinned_border_color_set(GtkColorButton* button, gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  GdkRGBA rgba{};
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
  char hex[8];
  std::snprintf(hex, sizeof(hex), "#%02x%02x%02x", static_cast<int>(rgba.red * 255),
                static_cast<int>(rgba.green * 255), static_cast<int>(rgba.blue * 255));
  self->config_.pinned_border_color = hex;
  self->save();
}

void SettingsWindow::on_pinned_focused_border_color_set(GtkColorButton* button,
                                                          gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  GdkRGBA rgba{};
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
  char hex[8];
  std::snprintf(hex, sizeof(hex), "#%02x%02x%02x", static_cast<int>(rgba.red * 255),
                static_cast<int>(rgba.green * 255), static_cast<int>(rgba.blue * 255));
  self->config_.pinned_focused_border_color = hex;
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

// -- Audio tab -------------------------------------------------------------

GtkWidget* SettingsWindow::build_audio_tab() {
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(box, 16);
  gtk_widget_set_margin_end(box, 16);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);

  GtkWidget* heading = gtk_label_new("Master volume");
  gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
  PangoAttrList* attrs = pango_attr_list_new();
  pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
  gtk_label_set_attributes(GTK_LABEL(heading), attrs);
  pango_attr_list_unref(attrs);
  gtk_box_append(GTK_BOX(box), heading);

  gtk_box_append(GTK_BOX(box), build_audio_master_row());

  audio_unavailable_label_ = gtk_label_new("Audio unavailable (no PipeWire)");
  gtk_widget_set_visible(audio_unavailable_label_, FALSE);
  gtk_box_append(GTK_BOX(box), audio_unavailable_label_);

  GtkWidget* separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_margin_top(separator, 4);
  gtk_widget_set_margin_bottom(separator, 4);
  gtk_box_append(GTK_BOX(box), separator);

  GtkWidget* apps_heading = gtk_label_new("Applications");
  gtk_label_set_xalign(GTK_LABEL(apps_heading), 0.0f);
  PangoAttrList* apps_attrs = pango_attr_list_new();
  pango_attr_list_insert(apps_attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
  gtk_label_set_attributes(GTK_LABEL(apps_heading), apps_attrs);
  pango_attr_list_unref(apps_attrs);
  gtk_box_append(GTK_BOX(box), apps_heading);

  audio_streams_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_append(GTK_BOX(box), audio_streams_box_);

  audio_mixer_.start(
      [this](int percent, bool muted, bool available) {
        on_audio_master_update(percent, muted, available);
      },
      [this](const std::vector<common::AudioStream>& streams) {
        on_audio_streams_update(streams);
      });

  return box;
}

GtkWidget* SettingsWindow::build_audio_master_row() {
  GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

  audio_master_mute_button_ = gtk_button_new();
  GtkWidget* icon = gtk_image_new_from_icon_name("audio-volume-high-symbolic");
  gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
  gtk_button_set_child(GTK_BUTTON(audio_master_mute_button_), icon);
  g_signal_connect(audio_master_mute_button_, "clicked", G_CALLBACK(on_audio_master_mute_toggled),
                    this);
  gtk_box_append(GTK_BOX(row), audio_master_mute_button_);

  audio_master_slider_ = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_widget_set_hexpand(audio_master_slider_, TRUE);
  gtk_scale_set_draw_value(GTK_SCALE(audio_master_slider_), TRUE);
  g_signal_connect(audio_master_slider_, "value-changed",
                    G_CALLBACK(on_audio_master_slider_changed), this);
  gtk_box_append(GTK_BOX(row), audio_master_slider_);

  return row;
}

GtkWidget* SettingsWindow::build_audio_stream_row(const common::AudioStream& stream) {
  GtkWidget* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

  GtkWidget* label = gtk_label_new(stream.label.c_str());
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(row), label);

  GtkWidget* slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_range_set_value(GTK_RANGE(slider), stream.volume_percent);
  gtk_scale_set_draw_value(GTK_SCALE(slider), TRUE);
  g_object_set_data(G_OBJECT(slider), "fleetwm-node-id", GUINT_TO_POINTER(stream.node_id));
  g_signal_connect(slider, "value-changed", G_CALLBACK(on_audio_stream_slider_changed), this);
  gtk_box_append(GTK_BOX(row), slider);

  audio_stream_sliders_[stream.node_id] = slider;
  return row;
}

void SettingsWindow::on_audio_master_update(int percent, bool muted, bool available) {
  gtk_widget_set_visible(audio_unavailable_label_, !available);
  gtk_widget_set_sensitive(audio_master_slider_, available);
  gtk_widget_set_sensitive(audio_master_mute_button_, available);
  if (!available) {
    return;
  }
  audio_updating_from_report_ = true;
  gtk_range_set_value(GTK_RANGE(audio_master_slider_), percent);
  audio_updating_from_report_ = false;

  GtkWidget* current_icon = gtk_button_get_child(GTK_BUTTON(audio_master_mute_button_));
  const char* icon_name = muted             ? "audio-volume-muted-symbolic"
                           : percent >= 50   ? "audio-volume-high-symbolic"
                           : percent > 0     ? "audio-volume-medium-symbolic"
                                              : "audio-volume-low-symbolic";
  gtk_image_set_from_icon_name(GTK_IMAGE(current_icon), icon_name);
}

void SettingsWindow::on_audio_streams_update(const std::vector<common::AudioStream>& streams) {
  bool set_changed = streams.size() != audio_stream_sliders_.size();
  if (!set_changed) {
    for (const auto& stream : streams) {
      if (!audio_stream_sliders_.count(stream.node_id)) {
        set_changed = true;
        break;
      }
    }
  }

  if (set_changed) {
    GtkWidget* child = gtk_widget_get_first_child(audio_streams_box_);
    while (child) {
      GtkWidget* next = gtk_widget_get_next_sibling(child);
      gtk_box_remove(GTK_BOX(audio_streams_box_), child);
      child = next;
    }
    audio_stream_sliders_.clear();
    for (const auto& stream : streams) {
      gtk_box_append(GTK_BOX(audio_streams_box_), build_audio_stream_row(stream));
    }
    return;
  }

  audio_updating_from_report_ = true;
  for (const auto& stream : streams) {
    auto it = audio_stream_sliders_.find(stream.node_id);
    if (it != audio_stream_sliders_.end()) {
      gtk_range_set_value(GTK_RANGE(it->second), stream.volume_percent);
    }
  }
  audio_updating_from_report_ = false;
}

void SettingsWindow::on_audio_master_slider_changed(GtkRange* range, gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  if (self->audio_updating_from_report_) {
    return;
  }
  self->audio_mixer_.set_master_volume(static_cast<int>(gtk_range_get_value(range) + 0.5));
}

void SettingsWindow::on_audio_master_mute_toggled(GtkButton*, gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  GtkWidget* current_icon = gtk_button_get_child(GTK_BUTTON(self->audio_master_mute_button_));
  const char* current_name = nullptr;
  g_object_get(current_icon, "icon-name", &current_name, nullptr);
  bool currently_muted = current_name && std::string(current_name) == "audio-volume-muted-symbolic";
  self->audio_mixer_.set_master_muted(!currently_muted);
}

void SettingsWindow::on_audio_stream_slider_changed(GtkRange* range, gpointer user_data) {
  auto* self = static_cast<SettingsWindow*>(user_data);
  if (self->audio_updating_from_report_) {
    return;
  }
  uint32_t node_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(range), "fleetwm-node-id"));
  self->audio_mixer_.set_stream_volume(node_id, static_cast<int>(gtk_range_get_value(range) + 0.5));
}

}  // namespace fleetwm::settings
