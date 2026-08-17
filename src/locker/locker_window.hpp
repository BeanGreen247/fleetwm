#pragma once

#include <gtk/gtk.h>

#include <string>

#include "app_style.hpp"
#include "ipc_client.hpp"
#include "theme.hpp"

namespace fleetwm::locker {

// The lock screen UI: a single gtk4-layer-shell OVERLAY-layer surface,
// anchored to all four edges (fullscreen) with KEYBOARD_MODE_EXCLUSIVE, so
// wlroots pins seat keyboard focus to it for as long as it's mapped --
// unlike src/greeter-login (a plain xdg_toplevel against the greeter's own
// throwaway compositor, which has no layer-shell), this runs as a normal
// client of the *real*, already-running compositor, which already
// implements layer-shell for the bar/launcher/wallpaper.
//
// Always shows exactly one user -- whoever getuid() says is running this
// session, via getpwuid() -- unlike the greeter's multi-account picker;
// there is no "switch user" concept for a lock screen. Verifies the
// entered password in-process via pam_verify.hpp (this process already
// runs as the target user, so no privilege-separated helper is needed the
// way fleetwm-greet's root process is for the actual login), and on
// success sends "UNLOCK" over the compositor IPC socket
// (src/common/ipc_client.hpp) and quits -- see ipc_server.cpp's SO_PEERCRED
// check on the receiving end for why this can't be spoofed by some other
// process just sending "UNLOCK" itself.
class LockerWindow {
 public:
  explicit LockerWindow(GtkApplication* app);

 private:
  static void on_activate(GtkApplication* app, gpointer user_data);
  void build(GtkApplication* app);
  void apply_theme();

  void attempt_unlock();
  void set_busy(bool busy);
  void show_error(const std::string& message);

  static void on_unlock_clicked(GtkButton* button, gpointer user_data);
  static void on_password_activate(GtkEntry* entry, gpointer user_data);

  ThemeConfig theme_config_;
  IpcClient ipc_;
  std::string username_;

  GtkWidget* window_ = nullptr;
  GtkWidget* password_entry_ = nullptr;
  GtkWidget* unlock_button_ = nullptr;
  GtkWidget* error_label_ = nullptr;
};

}  // namespace fleetwm::locker
