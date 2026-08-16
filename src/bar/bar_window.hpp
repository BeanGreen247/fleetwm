#pragma once

#include <gtk/gtk.h>

#include <array>
#include <string>

#include "app_style.hpp"
#include "bar_config.hpp"
#include "battery_source.hpp"
#include "ipc_client.hpp"
#include "theme.hpp"
#include "volume_source.hpp"

namespace fleetwm::bar {

// The bar's single long-lived layer-shell surface (top-anchored, exclusive
// zone reserved -- see ADR 0009), plus the shared IPC connection and the
// single 1Hz tick timer that drives both the clock and the CPU% sample
// (ADR 0005: "piggybacks on the same 1Hz GLib timeout that already drives
// the clock -- no additional wakeup source"). Unlike LauncherWindow/
// SettingsWindow, this process is long-lived: it stays running for the
// whole session, not spawned fresh per invocation.
class BarWindow {
 public:
  explicit BarWindow(GtkApplication* app);

 private:
  static void on_activate(GtkApplication* app, gpointer user_data);
  void build(GtkApplication* app);

  static gboolean on_ipc_readable(GIOChannel* channel, GIOCondition condition,
                                   gpointer user_data);
  static gboolean on_reconnect_tick(gpointer user_data);
  void try_connect();
  void handle_ipc_line(const std::string& line);

  static gboolean on_clock_tick(gpointer user_data);

  void set_workspace_button_labels();
  void on_workspace_button_clicked(int index);
  static void on_workspace_button_clicked_c(GtkButton* button, gpointer user_data);

  void build_power_menu(GtkWidget* parent_box);
  static void on_power_action(GSimpleAction* action, GVariant* parameter, gpointer user_data);

  void init_stats();
  void update_cpu_stat();
  void update_gpu_stat();
  static gboolean on_disk_tick(gpointer user_data);
  void update_disk_stat();

  // Battery indicator (iOS-style pill with the percentage drawn inside)
  // plus the power-mode icon next to it -- both hidden entirely on
  // desktops with no battery (BatterySource::Reading::available).
  void build_battery_indicator(GtkWidget* parent_box);
  void update_power_mode_icon();
  static void on_battery_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height,
                               gpointer user_data);

  // Reloads theme_config_/bar_config_ from disk and re-applies the
  // border/shape/clock-format that depend on them. Both configs are
  // watched live via GFileMonitor (GIO's own file-watch API, the natural
  // fit for a GLib-mainloop app -- mirrors the compositor's own raw
  // inotify watch on theme.toml, same "react to the file itself
  // changing" philosophy, just via the GLib-native equivalent).
  void start_config_watch();
  void apply_theme();
  void reload_theme();
  void reload_bar_config();

  static void on_theme_file_changed(GFileMonitor* monitor, GFile* file, GFile* other_file,
                                     GFileMonitorEvent event_type, gpointer user_data);
  static void on_bar_config_file_changed(GFileMonitor* monitor, GFile* file, GFile* other_file,
                                          GFileMonitorEvent event_type, gpointer user_data);

  ThemeConfig theme_config_;
  BarConfig bar_config_;
  GFileMonitor* theme_monitor_ = nullptr;
  GFileMonitor* bar_config_monitor_ = nullptr;

  GtkWidget* window_ = nullptr;
  GtkWidget* root_box_ = nullptr;  // needed to re-apply CSS classes on theme reload
  GtkWidget* clock_label_ = nullptr;
  GtkWidget* cpu_label_ = nullptr;
  GtkWidget* gpu_label_ = nullptr;
  GtkWidget* disk_label_ = nullptr;
  GtkWidget* volume_label_ = nullptr;
  std::array<GtkWidget*, 10> workspace_buttons_{};
  int active_workspace_ = 0;

  IpcClient ipc_;
  GIOChannel* ipc_channel_ = nullptr;
  guint ipc_watch_id_ = 0;
  guint reconnect_timer_id_ = 0;

  VolumeSource volume_source_;
  BatterySource battery_source_;
  BatterySource::Reading battery_reading_;
  GtkWidget* battery_area_ = nullptr;
  GtkWidget* power_mode_icon_ = nullptr;

  // /proc/stat aggregate-line samples, used to compute a CPU% delta
  // between consecutive 1Hz ticks (ADR 0005).
  unsigned long long prev_cpu_idle_ = 0;
  unsigned long long prev_cpu_total_ = 0;
  bool have_prev_cpu_sample_ = false;

  // GPU vendor sysfs path probed once at startup; empty means "no
  // supported vendor sysfs node found" (ADR 0005 GPU% fallback order).
  std::string gpu_sysfs_path_;
  bool gpu_uses_nvidia_smi_ = false;
  int gpu_tick_counter_ = 0;
};

}  // namespace fleetwm::bar
