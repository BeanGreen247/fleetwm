#include "bar_window.hpp"

#include <gtk4-layer-shell.h>
#include <sys/statvfs.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

namespace fleetwm::bar {

namespace {

constexpr int kBarHeightPx = 24;
constexpr guint kReconnectIntervalMs = 2000;

// Island mode (BarConfig::BarMode::Island): detached pill, centered
// horizontally (per wlr-layer-shell: anchored to a single edge only ->
// centered on the perpendicular axis) with a small gap from the top
// screen edge instead of being flush against it.
constexpr int kIslandTopMarginPx = 5;
// Explicit, not -1: same "GTK's size-to-content sentinel forwards as
// (uint32_t)-1 and wlroots' strict protocol validation fatally rejects
// it" trap as the height comment above -- once left/right anchors are
// off, width is unconstrained too and needs a real client-chosen value.
// Generous enough for the full stat/workspace/battery content at once.
constexpr int kIslandWidthPx = 720;

// First monitor's width, used to gate island mode to reasonably wide
// screens (ADR-less explicit user threshold: 1366px, matching common
// laptop panels) -- a floating pill has no room to breathe on anything
// smaller.
int primary_monitor_width_px() {
  GdkDisplay* display = gdk_display_get_default();
  if (!display) {
    return 0;
  }
  GListModel* monitors = gdk_display_get_monitors(display);
  if (!monitors || g_list_model_get_n_items(monitors) == 0) {
    return 0;
  }
  auto* monitor = static_cast<GdkMonitor*>(g_list_model_get_item(monitors, 0));
  GdkRectangle geometry{};
  gdk_monitor_get_geometry(monitor, &geometry);
  g_object_unref(monitor);
  return geometry.width;
}

}  // namespace

BarWindow::BarWindow(GtkApplication* app) {
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), this);
}

void BarWindow::on_activate(GtkApplication* app, gpointer user_data) {
  static_cast<BarWindow*>(user_data)->build(app);
}

void BarWindow::build(GtkApplication* app) {
  theme_config_ = load_theme_config();
  bar_config_ = load_bar_config();

  window_ = gtk_application_window_new(app);
  gtk_window_set_decorated(GTK_WINDOW(window_), FALSE);

  gtk_layer_init_for_window(GTK_WINDOW(window_));
  gtk_layer_set_layer(GTK_WINDOW(window_), GTK_LAYER_SHELL_LAYER_TOP);
  gtk_layer_set_keyboard_mode(GTK_WINDOW(window_), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
  apply_bar_layout();
  // "fleetwm-bar" on the window itself matches themes/base.css's own
  // ".fleetwm-bar { background-color: ...; color: ...; }" selector --
  // gives the bar its background/text color straight from the active
  // theme file via apply_app_style(), no bar-specific color code needed
  // for that part at all.
  gtk_widget_add_css_class(window_, "fleetwm-bar");
  // Corner radius (pill vs rectangle) is dynamic -- corner_style lives
  // in theme.toml, not hardcoded per theme file -- so it still comes
  // from this process's own small CSS provider (apply_theme() below) via
  // a second class. No border here: matches i3/dwm, where neither draws
  // an outline around the bar itself, only around focused client
  // windows (this compositor's own View::resize_border()).
  gtk_widget_add_css_class(window_, "fleetwm-bar-radius");

  GtkWidget* bar_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  root_box_ = bar_box;
  gtk_widget_set_margin_start(bar_box, 8);
  gtk_widget_set_margin_end(bar_box, 8);

  // Left section: workspace switcher. i3bar-style tight number boxes --
  // gtk_button_new_with_label()'s default GTK theming has generous
  // padding/min-size baked in via the "button" CSS node, so every
  // workspace button uses themes/base.css's own ".fleetwm-workspace-button"
  // class (background/radius from the theme + corner-style files) with
  // bar.toml's user-configurable colors layered on top as an inline
  // override in apply_theme() below.
  GtkWidget* workspace_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
  for (int i = 0; i < static_cast<int>(workspace_buttons_.size()); ++i) {
    char label[4];
    std::snprintf(label, sizeof(label), "%d", (i + 1) % 10);  // 1-9 then 0, matches kWorkspaceCount keybind convention
    GtkWidget* button = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(button, "fleetwm-workspace-button");
    g_object_set_data(G_OBJECT(button), "workspace-index", GINT_TO_POINTER(i));
    g_signal_connect(button, "clicked", G_CALLBACK(on_workspace_button_clicked_c), this);
    workspace_buttons_[static_cast<size_t>(i)] = button;
    gtk_box_append(GTK_BOX(workspace_box), button);
  }
  set_workspace_button_labels();
  gtk_box_append(GTK_BOX(bar_box), workspace_box);

  // Center section: clock/date only -- explicit user request to drop
  // the focused-window title entirely and put the clock in the middle
  // instead. A left spacer with matching hexpand keeps the clock
  // genuinely centered in the bar rather than just left-aligned after
  // the workspace buttons (GtkBox has no built-in "center child"
  // concept, so two equal-hexpand spacers on either side of the clock
  // is the standard way to fake one).
  GtkWidget* left_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(left_spacer, TRUE);
  gtk_box_append(GTK_BOX(bar_box), left_spacer);

  clock_label_ = gtk_label_new("--:--:--");
  gtk_widget_add_css_class(clock_label_, "fleetwm-clock");
  gtk_box_append(GTK_BOX(bar_box), clock_label_);

  GtkWidget* right_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(right_spacer, TRUE);
  gtk_box_append(GTK_BOX(bar_box), right_spacer);

  // Right section: stats, power icon.
  GtkWidget* right_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

  cpu_label_ = gtk_label_new("CPU --%");
  gpu_label_ = gtk_label_new("GPU --%");
  disk_label_ = gtk_label_new("Disk --%");
  volume_label_ = gtk_label_new("Vol --%");
  for (GtkWidget* stat : {cpu_label_, gpu_label_, disk_label_, volume_label_}) {
    gtk_widget_add_css_class(stat, "fleetwm-stat");
  }
  gtk_box_append(GTK_BOX(right_box), cpu_label_);
  gtk_box_append(GTK_BOX(right_box), gpu_label_);
  gtk_box_append(GTK_BOX(right_box), disk_label_);
  gtk_box_append(GTK_BOX(right_box), volume_label_);

  build_battery_indicator(right_box);
  build_power_menu(right_box);

  gtk_box_append(GTK_BOX(bar_box), right_box);

  gtk_window_set_child(GTK_WINDOW(window_), bar_box);
  gtk_window_present(GTK_WINDOW(window_));

  apply_theme();
  start_config_watch();

  init_stats();
  try_connect();
  g_timeout_add_seconds(1, on_clock_tick, this);
  g_timeout_add_seconds(5, on_disk_tick, this);
}

namespace {

// theme.hpp's parse_hex_color() returns [0,1] floats sized for
// wlr_scene_rect_set_color(); the bar needs the same "#rrggbb" string
// back out for CSS, with graceful fallback to the input on a malformed
// hex value (rather than silently rendering black/transparent).
std::string normalize_hex_color(const std::string& hex, const char* fallback) {
  float rgba[4];
  if (!parse_hex_color(hex, rgba)) {
    return fallback;
  }
  char buf[8];
  std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", static_cast<int>(rgba[0] * 255),
                static_cast<int>(rgba[1] * 255), static_cast<int>(rgba[2] * 255));
  return buf;
}

}  // namespace

void BarWindow::apply_bar_layout() {
  bool island = bar_config_.bar_mode == BarMode::Island &&
                primary_monitor_width_px() >= kIslandMinOutputWidthPx;

  gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_LEFT, !island);
  gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_RIGHT, !island);
  gtk_layer_set_margin(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_TOP,
                        island ? kIslandTopMarginPx : 0);
  // Island floats above tiled windows rather than reserving a full-width
  // strip (0 = no exclusive zone) -- Full keeps the existing behavior of
  // reserving the bar's height so windows tile starting below it.
  gtk_layer_set_exclusive_zone(GTK_WINDOW(window_), island ? 0 : kBarHeightPx);
  // Explicit width/height, never -1: gtk4-layer-shell forwards GTK's
  // "size to content" sentinel straight through to
  // zwlr_layer_surface_v1.set_size() as (uint32_t)-1, which wlroots'
  // strict protocol validation fatally rejects -- same crash trap
  // documented in launcher_window.cpp. Full's width is irrelevant once
  // both left/right anchors force the compositor to dictate it via
  // configure, but island (anchored on one axis only) actually uses the
  // requested width per the wlr-layer-shell spec.
  gtk_window_set_default_size(GTK_WINDOW(window_), island ? kIslandWidthPx : 100, kBarHeightPx);
}

void BarWindow::apply_theme() {
  // Background/text colors, structural rules, and the theme's default
  // accent all come from the installed theme CSS stack now (themes/*.css
  // -- see ADR and app_style.hpp) instead of being hand-rolled here.
  apply_app_style(theme_config_);

  static GtkCssProvider* provider = nullptr;
  if (!provider) {
    provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  }

  // Rounded => pill (fully-rounded short ends: radius = half the bar
  // height), Sharp => plain rectangle -- same corner_style toggle that
  // already drives window corners, reused as-is per explicit user
  // choice not to add a bar-specific shape setting. Workspace buttons'
  // own radius already comes from themes/corners-*.css (loaded by
  // apply_app_style() above), so only the bar window's own radius needs
  // a dynamic override here (no border -- see build()'s comment).
  bool rounded = theme_config_.corner_style == CornerStyle::Rounded;
  int bar_radius = rounded ? kBarHeightPx / 2 : 0;

  // Workspace button colors are the one piece that's genuinely dynamic
  // per-user (bar.toml, not a static theme file) -- layered as an
  // override on top of themes/base.css's own ".fleetwm-workspace-button"
  // rule. ".fleetwm-workspace-button, .fleetwm-workspace-button button"
  // (not just the plain class) covers the power icon too: GtkMenuButton
  // has its background/border painted on an internal nested "button" CSS
  // node, not the outer "menubutton" node its class attaches to --
  // classes don't cascade to descendant nodes in GTK's CSS model. Plain
  // GtkButtons (the workspace buttons themselves) only ever match the
  // first half of the selector since the class sits directly on their
  // own "button" node; harmless to include both forms for every button.
  const WorkspaceColors& wc = bar_config_.workspace_colors;
  std::string inactive_bg = normalize_hex_color(wc.inactive_bg, "#3c3c3c");
  std::string inactive_fg = normalize_hex_color(wc.inactive_fg, "#ffffff");
  std::string active_bg = normalize_hex_color(wc.active_bg, "#ff7800");
  std::string active_fg = normalize_hex_color(wc.active_fg, "#000000");
  std::string ws_selector = ".fleetwm-workspace-button, .fleetwm-workspace-button button";
  std::string ws_active_selector =
      ".fleetwm-workspace-button.active, .fleetwm-workspace-button.active button";

  std::string css = ".fleetwm-bar-radius { border-radius: " + std::to_string(bar_radius) +
                     "px; } " + ws_selector + " { background: " + inactive_bg +
                     "; color: " + inactive_fg + "; } " + ws_active_selector +
                     " { background: " + active_bg + "; color: " + active_fg + "; }";
  gtk_css_provider_load_from_string(provider, css.c_str());
}

void BarWindow::start_config_watch() {
  GFile* theme_file = g_file_new_for_path(user_config_path().c_str());
  theme_monitor_ = g_file_monitor_file(theme_file, G_FILE_MONITOR_NONE, nullptr, nullptr);
  g_object_unref(theme_file);
  if (theme_monitor_) {
    g_signal_connect(theme_monitor_, "changed", G_CALLBACK(on_theme_file_changed), this);
  }

  GFile* bar_file = g_file_new_for_path(bar_user_config_path().c_str());
  bar_config_monitor_ = g_file_monitor_file(bar_file, G_FILE_MONITOR_NONE, nullptr, nullptr);
  g_object_unref(bar_file);
  if (bar_config_monitor_) {
    g_signal_connect(bar_config_monitor_, "changed", G_CALLBACK(on_bar_config_file_changed),
                      this);
  }
}

void BarWindow::on_theme_file_changed(GFileMonitor*, GFile*, GFile*, GFileMonitorEvent event_type,
                                       gpointer user_data) {
  // GFileMonitor emits several event types per save (CHANGES_DONE_HINT
  // is the "settled" one for a plain in-place write; a rename-into-place
  // save instead delivers RENAMED/MOVED_IN) -- reloading on every event
  // is harmless (reload_theme_config-equivalent here is cheap and
  // idempotent, matching the compositor's own inotify handler), so no
  // need to filter by event_type at all.
  (void)event_type;
  static_cast<BarWindow*>(user_data)->reload_theme();
}

void BarWindow::on_bar_config_file_changed(GFileMonitor*, GFile*, GFile*,
                                            GFileMonitorEvent event_type, gpointer user_data) {
  (void)event_type;
  static_cast<BarWindow*>(user_data)->reload_bar_config();
}

void BarWindow::reload_theme() {
  theme_config_ = load_theme_config();
  apply_theme();
}

void BarWindow::reload_bar_config() {
  bar_config_ = load_bar_config();
  apply_theme();  // workspace_colors live in bar_config_, not theme_config_
  update_power_mode_icon();
  apply_bar_layout();
}

void BarWindow::try_connect() {
  if (ipc_.connect()) {
    ipc_channel_ = g_io_channel_unix_new(ipc_.fd());
    ipc_watch_id_ = g_io_add_watch(
        ipc_channel_, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR), on_ipc_readable,
        this);
    ipc_.send_command("WORKSPACE?");
    return;
  }
  // Compositor not up yet (or connection dropped) -- retry periodically
  // rather than treating this as fatal, matching IpcClient's own
  // "degrade gracefully" contract.
  if (reconnect_timer_id_ == 0) {
    reconnect_timer_id_ = g_timeout_add(kReconnectIntervalMs, on_reconnect_tick, this);
  }
}

gboolean BarWindow::on_reconnect_tick(gpointer user_data) {
  auto* self = static_cast<BarWindow*>(user_data);
  if (self->ipc_.is_connected()) {
    self->reconnect_timer_id_ = 0;
    return G_SOURCE_REMOVE;
  }
  self->try_connect();
  return G_SOURCE_CONTINUE;
}

gboolean BarWindow::on_ipc_readable(GIOChannel*, GIOCondition condition, gpointer user_data) {
  auto* self = static_cast<BarWindow*>(user_data);

  self->ipc_.poll_lines([self](const std::string& line) { self->handle_ipc_line(line); });

  if ((condition & (G_IO_HUP | G_IO_ERR)) || !self->ipc_.is_connected()) {
    if (self->ipc_channel_) {
      g_io_channel_unref(self->ipc_channel_);
      self->ipc_channel_ = nullptr;
    }
    self->ipc_watch_id_ = 0;
    self->try_connect();  // starts the reconnect timer
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

void BarWindow::handle_ipc_line(const std::string& line) {
  if (line.rfind("WORKSPACE_CHANGED ", 0) == 0) {
    try {
      active_workspace_ = std::stoi(line.substr(18));
    } catch (...) {
      return;
    }
    set_workspace_button_labels();
    return;
  }
  // Bare digit(s): the reply to our own WORKSPACE? query at connect time.
  if (!line.empty() && line.find_first_not_of("0123456789") == std::string::npos) {
    try {
      active_workspace_ = std::stoi(line);
    } catch (...) {
      return;
    }
    set_workspace_button_labels();
    return;
  }
  // FOCUSED_TITLE is intentionally ignored -- the bar no longer shows
  // the focused window's title (explicit user request; the compositor
  // still broadcasts it over IPC in case some other future client wants
  // it, see ADR 0003).
}

void BarWindow::set_workspace_button_labels() {
  for (int i = 0; i < static_cast<int>(workspace_buttons_.size()); ++i) {
    GtkWidget* button = workspace_buttons_[static_cast<size_t>(i)];
    if (i == active_workspace_) {
      gtk_widget_add_css_class(button, "active");
    } else {
      gtk_widget_remove_css_class(button, "active");
    }
  }
}

void BarWindow::on_workspace_button_clicked_c(GtkButton* button, gpointer user_data) {
  int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "workspace-index"));
  static_cast<BarWindow*>(user_data)->on_workspace_button_clicked(index);
}

void BarWindow::on_workspace_button_clicked(int index) {
  ipc_.send_command("WORKSPACE " + std::to_string(index));
  // No optimistic local update: the compositor's WORKSPACE_CHANGED
  // broadcast (sent to every connected client, including us) is the
  // single source of truth set_workspace_button_labels() reacts to --
  // matches how a keybind-driven switch already updates the bar.
}

gboolean BarWindow::on_clock_tick(gpointer user_data) {
  auto* self = static_cast<BarWindow*>(user_data);
  const ClockFormat& fmt = self->bar_config_.clock;

  time_t now = time(nullptr);
  tm local_tm{};
  localtime_r(&now, &local_tm);

  char time_buf[16];
  std::strftime(time_buf, sizeof(time_buf), fmt.show_seconds ? "%H:%M:%S" : "%H:%M", &local_tm);

  std::string label = time_buf;
  if (fmt.show_date) {
    // Assemble only the enabled date components, in year/month/day
    // order, space-separated -- rather than a single strftime pattern,
    // since any subset can be toggled off independently.
    std::string date_part;
    if (fmt.show_year) {
      char y[8];
      std::strftime(y, sizeof(y), "%Y", &local_tm);
      date_part += y;
    }
    if (fmt.show_month) {
      char m[8];
      std::strftime(m, sizeof(m), "%m", &local_tm);
      if (!date_part.empty()) date_part += "-";
      date_part += m;
    }
    if (fmt.show_day) {
      char d[8];
      std::strftime(d, sizeof(d), "%d", &local_tm);
      if (!date_part.empty()) date_part += "-";
      date_part += d;
    }
    if (!date_part.empty()) {
      label += "  " + date_part;
    }
  }
  gtk_label_set_text(GTK_LABEL(self->clock_label_), label.c_str());

  self->update_cpu_stat();
  self->update_gpu_stat();

  return G_SOURCE_CONTINUE;
}

void BarWindow::build_power_menu(GtkWidget* parent_box) {
  GMenu* menu = g_menu_new();
  g_menu_append(menu, "Log out", "bar.logout");
  g_menu_append(menu, "Reboot", "bar.reboot");
  g_menu_append(menu, "Shut down", "bar.shutdown");

  GSimpleActionGroup* action_group = g_simple_action_group_new();
  const char* action_names[] = {"logout", "reboot", "shutdown"};
  for (const char* name : action_names) {
    GSimpleAction* action = g_simple_action_new(name, nullptr);
    g_signal_connect(action, "activate", G_CALLBACK(on_power_action), this);
    g_action_map_add_action(G_ACTION_MAP(action_group), G_ACTION(action));
    g_object_unref(action);
  }
  gtk_widget_insert_action_group(window_, "bar", G_ACTION_GROUP(action_group));
  g_object_unref(action_group);

  GtkWidget* menu_button = gtk_menu_button_new();
  // gtk_menu_button_set_icon_name()'s internal GtkImage defaults to a
  // larger symbolic-icon size than fits the workspace buttons' 16px
  // min-height, stretching the whole button (and the bar's own
  // exclusive-zone-sized height along with it) taller than intended --
  // CSS's "-gtk-icon-size" only accepts named sizes (normal/large), not
  // raw pixels, so it had no effect. Building the GtkImage directly and
  // pinning its pixel size explicitly is the only reliable fix.
  GtkWidget* icon = gtk_image_new_from_icon_name("system-shutdown-symbolic");
  gtk_image_set_pixel_size(GTK_IMAGE(icon), 12);
  gtk_menu_button_set_child(GTK_MENU_BUTTON(menu_button), icon);
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button), G_MENU_MODEL(menu));
  // Same flat box treatment as the workspace buttons
  // (.fleetwm-workspace-button CSS, set up in apply_theme()) rather than
  // GTK's default menu-button chrome, so the power icon reads as part of
  // the same button family instead of standing out as a leftover
  // system-themed control -- explicit user request, distinct from
  // themes/base.css's own icon-only ".fleetwm-power-icon" style.
  gtk_widget_add_css_class(menu_button, "fleetwm-workspace-button");
  g_object_unref(menu);
  gtk_box_append(GTK_BOX(parent_box), menu_button);
}

void BarWindow::on_power_action(GSimpleAction* action, GVariant*, gpointer) {
  const char* name = g_action_get_name(G_ACTION(action));
  const char* argv[3] = {nullptr, nullptr, nullptr};
  if (std::strcmp(name, "logout") == 0) {
    argv[0] = "loginctl";
    argv[1] = "terminate-session";
    argv[2] = std::getenv("XDG_SESSION_ID");
  } else if (std::strcmp(name, "reboot") == 0) {
    argv[0] = "systemctl";
    argv[1] = "reboot";
  } else if (std::strcmp(name, "shutdown") == 0) {
    argv[0] = "systemctl";
    argv[1] = "poweroff";
  } else {
    return;
  }
  GError* error = nullptr;
  char** child_argv = nullptr;
  int argc = 0;
  while (argv[argc] != nullptr) {
    ++argc;
  }
  child_argv = g_new0(char*, static_cast<guint>(argc) + 1);
  for (int i = 0; i < argc; ++i) {
    child_argv[i] = g_strdup(argv[i]);
  }
  if (!g_spawn_async(nullptr, child_argv, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr,
                      &error)) {
    std::fprintf(stderr, "fleetwm-bar: power action '%s' failed: %s\n", name,
                 error != nullptr ? error->message : "unknown error");
    g_clear_error(&error);
  }
  g_strfreev(child_argv);
}

// -- Stats (ADR 0005) ----------------------------------------------------

void BarWindow::init_stats() {
  // GPU vendor sysfs probe, once at startup (ADR 0005: "GPU vendor
  // detection happens once at bar startup ... not on every sample").
  // Priority order: amdgpu -> Intel -> nvidia-smi subprocess -> N/A.
  for (int card = 0; card < 8; ++card) {
    std::string amdgpu_path =
        "/sys/class/drm/card" + std::to_string(card) + "/device/gpu_busy_percent";
    if (std::ifstream(amdgpu_path).good()) {
      gpu_sysfs_path_ = amdgpu_path;
      break;
    }
    std::string intel_path =
        "/sys/class/drm/card" + std::to_string(card) + "/gt_busy_percent";
    if (std::ifstream(intel_path).good()) {
      gpu_sysfs_path_ = intel_path;
      break;
    }
  }
  if (gpu_sysfs_path_.empty()) {
    // which nvidia-smi
    FILE* pipe = popen("command -v nvidia-smi 2>/dev/null", "r");
    if (pipe) {
      char buf[256] = {};
      size_t n = std::fread(buf, 1, sizeof(buf) - 1, pipe);
      pclose(pipe);
      gpu_uses_nvidia_smi_ = n > 0;
    }
  }
  if (gpu_sysfs_path_.empty() && !gpu_uses_nvidia_smi_) {
    gtk_label_set_text(GTK_LABEL(gpu_label_), "GPU N/A");
  }

  update_disk_stat();

  volume_source_.start([this](int percent, bool available) {
    if (available) {
      gtk_label_set_text(GTK_LABEL(volume_label_), ("Vol " + std::to_string(percent) + "%").c_str());
    } else {
      gtk_label_set_text(GTK_LABEL(volume_label_), "Vol N/A");
    }
  });

  battery_source_.start([this](const BatterySource::Reading& reading) {
    battery_reading_ = reading;
    gtk_widget_set_visible(battery_area_, reading.available);
    gtk_widget_set_visible(power_mode_icon_, reading.available);
    if (reading.available) {
      std::string tooltip = std::to_string(reading.percent) + "% " +
                             (reading.charging ? "(charging)" : "(on battery)");
      if (reading.hours_remaining >= 0.0) {
        int total_minutes = static_cast<int>(reading.hours_remaining * 60.0 + 0.5);
        tooltip += " - " + std::to_string(total_minutes / 60) + "h " +
                   std::to_string(total_minutes % 60) + "m " +
                   (reading.charging ? "until full" : "remaining");
      }
      gtk_widget_set_tooltip_text(battery_area_, tooltip.c_str());
    }
    gtk_widget_queue_draw(battery_area_);
  });
}

void BarWindow::build_battery_indicator(GtkWidget* parent_box) {
  power_mode_icon_ = gtk_image_new();
  gtk_image_set_pixel_size(GTK_IMAGE(power_mode_icon_), 12);
  gtk_widget_add_css_class(power_mode_icon_, "fleetwm-stat");
  gtk_widget_set_visible(power_mode_icon_, FALSE);
  gtk_box_append(GTK_BOX(parent_box), power_mode_icon_);
  update_power_mode_icon();

  // iOS-style pill battery, drawn by hand (Cairo) rather than a stock
  // GTK/Adwaita battery icon, since none of those render the percentage
  // as text inside the outline the way iOS does -- explicit user request.
  battery_area_ = gtk_drawing_area_new();
  gtk_widget_set_size_request(battery_area_, 26, 14);
  gtk_widget_add_css_class(battery_area_, "fleetwm-stat");
  gtk_widget_set_visible(battery_area_, FALSE);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(battery_area_), on_battery_draw, this, nullptr);
  gtk_box_append(GTK_BOX(parent_box), battery_area_);
}

void BarWindow::update_power_mode_icon() {
  const char* icon_name = "power-profile-balanced-symbolic";
  switch (bar_config_.power_mode) {
    case PowerMode::Performance: icon_name = "power-profile-performance-symbolic"; break;
    case PowerMode::BatterySaver: icon_name = "power-profile-power-saver-symbolic"; break;
    case PowerMode::Normal: icon_name = "power-profile-balanced-symbolic"; break;
  }
  gtk_image_set_from_icon_name(GTK_IMAGE(power_mode_icon_), icon_name);
}

void BarWindow::on_battery_draw(GtkDrawingArea*, cairo_t* cr, int width, int height,
                                 gpointer user_data) {
  auto* self = static_cast<BarWindow*>(user_data);
  const BatterySource::Reading& r = self->battery_reading_;
  if (!r.available) {
    return;
  }

  GdkRGBA fg{};
  gtk_widget_get_color(self->battery_area_, &fg);

  // Body: rounded outline, leaving room on the right for the small nub
  // (the one detail that reads as "battery" rather than just "rounded
  // rect" -- iOS's own battery glyph has the same proportions).
  const double nub_w = 2.0;
  const double body_w = width - nub_w - 1.0;
  const double body_h = height;
  const double radius = 3.0;
  const double stroke_w = 1.3;

  cairo_set_line_width(cr, stroke_w);
  cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, fg.alpha);

  double x = stroke_w / 2.0, y = stroke_w / 2.0;
  double w = body_w - stroke_w, h = body_h - stroke_w;
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - radius, y + radius, radius, -M_PI_2, 0);
  cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, M_PI_2);
  cairo_arc(cr, x + radius, y + h - radius, radius, M_PI_2, M_PI);
  cairo_arc(cr, x + radius, y + radius, radius, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
  cairo_stroke(cr);

  // Nub, vertically centered against the body.
  double nub_h = body_h * 0.4;
  cairo_rectangle(cr, body_w, (body_h - nub_h) / 2.0, nub_w, nub_h);
  cairo_fill(cr);

  // Fill proportional to charge. Green while charging or comfortably
  // charged, amber under 20%, red under 10% and not charging -- same
  // thresholds as iOS's own battery glyph.
  double pct = std::max(0, std::min(100, r.percent)) / 100.0;
  double inset = stroke_w + 1.5;
  double fill_w = std::max(0.0, (body_w - 2 * inset) * pct);
  double fill_h = body_h - 2 * inset;
  if (r.charging || r.percent > 20) {
    cairo_set_source_rgba(cr, 0.30, 0.85, 0.39, 1.0);
  } else if (r.percent > 10) {
    cairo_set_source_rgba(cr, 1.0, 0.62, 0.04, 1.0);
  } else {
    cairo_set_source_rgba(cr, 1.0, 0.23, 0.19, 1.0);
  }
  if (fill_w > 0.0) {
    cairo_rectangle(cr, inset, inset, fill_w, fill_h);
    cairo_fill(cr);
  }

  // Percentage, centered inside the body.
  cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, fg.alpha);
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 8.5);
  std::string text = std::to_string(r.percent);
  cairo_text_extents_t extents{};
  cairo_text_extents(cr, text.c_str(), &extents);
  cairo_move_to(cr, x + w / 2.0 - extents.width / 2.0 - extents.x_bearing,
                y + h / 2.0 - extents.height / 2.0 - extents.y_bearing);
  cairo_show_text(cr, text.c_str());
}

void BarWindow::update_cpu_stat() {
  std::ifstream stat_file("/proc/stat");
  std::string line;
  if (!std::getline(stat_file, line) || line.rfind("cpu ", 0) != 0) {
    return;
  }
  std::istringstream iss(line.substr(4));
  unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
  iss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

  unsigned long long idle_total = idle + iowait;
  unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;

  if (have_prev_cpu_sample_) {
    unsigned long long total_delta = total - prev_cpu_total_;
    unsigned long long idle_delta = idle_total - prev_cpu_idle_;
    if (total_delta > 0) {
      int percent = static_cast<int>(
          100.0 * static_cast<double>(total_delta - idle_delta) / static_cast<double>(total_delta) +
          0.5);
      gtk_label_set_text(GTK_LABEL(cpu_label_), ("CPU " + std::to_string(percent) + "%").c_str());
    }
  }
  prev_cpu_idle_ = idle_total;
  prev_cpu_total_ = total;
  have_prev_cpu_sample_ = true;
}

void BarWindow::update_gpu_stat() {
  if (!gpu_sysfs_path_.empty()) {
    std::ifstream f(gpu_sysfs_path_);
    int percent = -1;
    if (f >> percent) {
      gtk_label_set_text(GTK_LABEL(gpu_label_), ("GPU " + std::to_string(percent) + "%").c_str());
    }
    return;
  }
  if (gpu_uses_nvidia_smi_) {
    // Reduced-frequency sampling (ADR 0005: "at a reduced 3s interval,
    // since spawning a process is comparatively expensive") -- this is
    // called from the 1Hz tick, so only actually spawn every 3rd call.
    if (++gpu_tick_counter_ < 3) {
      return;
    }
    gpu_tick_counter_ = 0;
    FILE* pipe =
        popen("nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null",
              "r");
    if (!pipe) {
      return;
    }
    int percent = -1;
    int scanned = std::fscanf(pipe, "%d", &percent);
    pclose(pipe);
    if (scanned == 1) {
      gtk_label_set_text(GTK_LABEL(gpu_label_), ("GPU " + std::to_string(percent) + "%").c_str());
    }
  }
}

gboolean BarWindow::on_disk_tick(gpointer user_data) {
  static_cast<BarWindow*>(user_data)->update_disk_stat();
  return G_SOURCE_CONTINUE;
}

void BarWindow::update_disk_stat() {
  struct statvfs vfs {};
  if (statvfs("/", &vfs) != 0) {
    gtk_label_set_text(GTK_LABEL(disk_label_), "Disk N/A");
    return;
  }
  unsigned long long total = vfs.f_blocks;
  unsigned long long free = vfs.f_bfree;
  if (total == 0) {
    return;
  }
  int percent = static_cast<int>(100.0 * static_cast<double>(total - free) /
                                      static_cast<double>(total) +
                                  0.5);
  gtk_label_set_text(GTK_LABEL(disk_label_), ("Disk " + std::to_string(percent) + "%").c_str());
}

}  // namespace fleetwm::bar
