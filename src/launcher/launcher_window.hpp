#pragma once

#include <gtk/gtk.h>

#include <string>
#include <vector>

#include "app_index.hpp"

namespace fleetwm::launcher {

// Builds and drives the launcher's single popup window: a search entry
// plus a result list mixing installed apps (from AppIndex) and a
// raw-shell-command fallback row. Launching an app or command, or
// pressing Escape, quits the GTK application (spawn-fresh-per-invocation
// model -- this process always exits after one action).
class LauncherWindow {
 public:
  explicit LauncherWindow(GtkApplication* app);

 private:
  static void on_activate(GtkApplication* app, gpointer user_data);

  void build(GtkApplication* app);
  void refresh_results(const std::string& query);
  void launch_selected();
  void launch_app(GDesktopAppInfo* info);
  void launch_command(const std::string& text);

  static void on_search_changed(GtkEditable* editable, gpointer user_data);
  static void on_row_activated(GtkListBox* box, GtkListBoxRow* row, gpointer user_data);
  static gboolean on_key_pressed(GtkEventControllerKey* controller, guint keyval,
                                  guint keycode, GdkModifierType state, gpointer user_data);

  AppIndex index_;
  GtkWidget* window_ = nullptr;
  GtkWidget* entry_ = nullptr;
  GtkWidget* list_box_ = nullptr;

  // Parallel to the rows currently shown in list_box_: nullptr means "run
  // the typed text as a shell command" (the synthetic fallback row).
  std::vector<const AppEntry*> current_results_;
};

}  // namespace fleetwm::launcher
