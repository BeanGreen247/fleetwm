#pragma once

#include <gtk/gtk.h>

#include "app_style.hpp"
#include "ipc_client.hpp"
#include "theme.hpp"

namespace fleetwm::powermenu {

// A fullscreen, keyboard-exclusive layer-shell overlay with a centered
// card of power actions (Lock/Log out/Sleep/Reboot/Shut down) -- replaces
// the bar's old GtkMenuButton popover (a GTK-managed xdg_popup), whose
// individual row clicks turned out to be unreliable against this
// compositor's popup input routing. A plain top-level layer-shell surface
// uses the exact same input path fleetwm-locker already relies on daily
// (a plain button inside a normal mapped surface, no xdg_popup involved),
// which is why this is a full extra window rather than a fix to the old
// popover.
//
// Same "solid full-screen fleetwm-panel background" as the locker for the
// "darkened screen" backdrop -- this compositor does not composite
// per-pixel CSS alpha for layer-shell surfaces (confirmed in an earlier
// session), so a real dimmed-through view of the desktop isn't available;
// an opaque themed backdrop is the closest equivalent, same tradeoff the
// locker already made.
class PowerMenuWindow {
 public:
  explicit PowerMenuWindow(GtkApplication* app);

 private:
  static void on_activate(GtkApplication* app, gpointer user_data);
  void build(GtkApplication* app);
  void apply_theme();

  void run_action(const char* name);
  void close();

  static void on_action_clicked(GtkButton* button, gpointer user_data);
  static void on_backdrop_clicked(GtkGestureClick* gesture, int n_press, double x, double y,
                                   gpointer user_data);
  static gboolean on_key_pressed(GtkEventControllerKey* controller, guint keyval, guint keycode,
                                  GdkModifierType state, gpointer user_data);

  ThemeConfig theme_config_;
  IpcClient ipc_;

  GtkWidget* window_ = nullptr;
  GtkWidget* card_ = nullptr;
};

}  // namespace fleetwm::powermenu
