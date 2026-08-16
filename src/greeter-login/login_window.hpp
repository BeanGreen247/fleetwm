#pragma once

#include <gtk/gtk.h>

#include <string>
#include <vector>

#include "app_style.hpp"
#include "theme.hpp"
#include "wallpaper_config.hpp"

namespace fleetwm::greeter_login {

// The login screen UI: a single fullscreen xdg_toplevel (there is no
// layer-shell in the greeter compositor it runs against -- see
// src/greeter/compositor.{hpp,cpp} -- so this is a plain
// GtkApplicationWindow, not gtk4-layer-shell like the bar/wallpaper).
// Talks to fleetwm-greet (the process that spawned it, still root, owns
// PAM) over `ipc_fd` using the login_ipc protocol; never touches PAM or
// any credential store itself.
//
// Shows a Windows-7-style user picker first (one square avatar tile per
// real login-capable local account, plus an "Other User" tile) when
// there is at least one such account, floating directly on the
// wallpaper with no card/box chrome -- falls straight through to the
// plain single-user form otherwise. Picking a tile locks the username
// to that account (large avatar + name, password-only); "Other User"
// is the only path that allows typing an arbitrary name.
class LoginWindow {
 public:
  LoginWindow(GtkApplication* app, int ipc_fd);

 private:
  static void on_activate(GtkApplication* app, gpointer user_data);
  void build(GtkApplication* app);

  GtkWidget* build_picker_page();
  GtkWidget* build_login_page();
  GtkWidget* build_power_row();
  GtkWidget* build_avatar(int size);
  GtkWidget* build_user_tile(const std::string& username);

  // Switches to the login form for `username`. An empty string means
  // "Other User" -- username field stays editable and empty; any other
  // value pre-fills the field and makes it read-only.
  void show_login_page(const std::string& username);
  void show_picker_page();

  void attempt_login();
  void request_power_action(const char* action);
  void set_busy(bool busy);
  void show_error(const std::string& message);

  static void on_login_clicked(GtkButton* button, gpointer user_data);
  static void on_password_activate(GtkEntry* entry, gpointer user_data);
  static gboolean on_ipc_readable(gint fd, GIOCondition condition, gpointer user_data);

  int ipc_fd_;
  ThemeConfig theme_config_;
  WallpaperConfig wallpaper_config_;
  // Real login-capable local accounts (UID >= 1000, a real shell), sorted
  // by username -- see list_login_users() in login_window.cpp for the
  // exact filter. Populated once at startup; a user picked up mid-session
  // (e.g. `useradd` while the greeter is already showing) simply won't
  // appear until the next login cycle, same staleness window the old
  // TTY prompt had for anything read at process start.
  std::vector<std::string> login_users_;

  GtkWidget* window_ = nullptr;
  GtkWidget* stack_ = nullptr;
  bool showing_picker_ = false;

  GtkWidget* back_button_ = nullptr;
  GtkWidget* login_avatar_box_ = nullptr;
  GtkWidget* login_name_label_ = nullptr;
  GtkWidget* username_entry_ = nullptr;
  GtkWidget* password_entry_ = nullptr;
  GtkWidget* error_label_ = nullptr;
  GtkWidget* login_button_ = nullptr;
};

}  // namespace fleetwm::greeter_login
