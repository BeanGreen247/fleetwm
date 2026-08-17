#include "locker_window.hpp"

#include <gtk4-layer-shell.h>
#include <pwd.h>
#include <unistd.h>

#include <cstdio>

#include "pam_verify.hpp"

namespace fleetwm::locker {

namespace {

// getpwuid(getuid())'s pw_name, or a numeric fallback so the UI never
// shows a blank username field -- getpwuid() failing at all (e.g. an
// NSS/LDAP hiccup) is rare but shouldn't make the lock screen unusable.
std::string current_username() {
  if (struct passwd* pw = getpwuid(getuid())) {
    return pw->pw_name;
  }
  return "user " + std::to_string(getuid());
}

}  // namespace

LockerWindow::LockerWindow(GtkApplication* app) {
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), this);
}

void LockerWindow::on_activate(GtkApplication* app, gpointer user_data) {
  static_cast<LockerWindow*>(user_data)->build(app);
}

void LockerWindow::build(GtkApplication* app) {
  theme_config_ = load_theme_config();
  username_ = current_username();
  ipc_.connect();  // best-effort; attempt_unlock() re-checks is_connected()

  window_ = gtk_application_window_new(app);
  gtk_window_set_decorated(GTK_WINDOW(window_), FALSE);
  // The CSS rule (themes/base.css) is "window.fleetwm-panel", matching a
  // GtkWindow element with this class directly -- every other themed
  // window in this codebase (launcher, settings) adds it to window_
  // itself, not a child box, for exactly this reason.
  gtk_widget_add_css_class(window_, "fleetwm-panel");

  gtk_layer_init_for_window(GTK_WINDOW(window_));
  gtk_layer_set_layer(GTK_WINDOW(window_), GTK_LAYER_SHELL_LAYER_OVERLAY);
  gtk_layer_set_keyboard_mode(GTK_WINDOW(window_), GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
  // All four edges anchored: a fullscreen surface, not a centered popup
  // like the launcher -- it must fully occlude every toplevel/the bar
  // underneath so a click or the mouse hovering past its bounds can never
  // hit-test through to a real window while locked (server_cursor_button/
  // process_cursor_motion in compositor/server.cpp don't special-case
  // "locked" for hit-testing at all; full screen coverage is what makes
  // that safe).
  gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);

  GtkWidget* backdrop = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_halign(backdrop, GTK_ALIGN_FILL);
  gtk_widget_set_valign(backdrop, GTK_ALIGN_FILL);

  GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_halign(card, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(card, GTK_ALIGN_CENTER);
  gtk_widget_set_vexpand(card, TRUE);
  gtk_widget_set_hexpand(card, TRUE);

  // Same avatar/name/entry/error CSS classes as greeter-login's login
  // page (themes/base.css) -- deliberately reusing that visual language
  // rather than inventing a second lock-screen look, per the explicit
  // "reused greeter" request this was built to.
  GtkWidget* avatar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(avatar, "fleetwm-greeter-avatar");
  gtk_widget_set_size_request(avatar, 128, 128);
  gtk_widget_set_halign(avatar, GTK_ALIGN_CENTER);
  GtkWidget* avatar_icon = gtk_image_new_from_icon_name("avatar-default-symbolic");
  gtk_image_set_pixel_size(GTK_IMAGE(avatar_icon), 85);
  gtk_widget_set_halign(avatar_icon, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(avatar_icon, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand(avatar_icon, TRUE);
  gtk_widget_set_vexpand(avatar_icon, TRUE);
  gtk_box_append(GTK_BOX(avatar), avatar_icon);
  gtk_box_append(GTK_BOX(card), avatar);

  GtkWidget* name_label = gtk_label_new(username_.c_str());
  gtk_widget_add_css_class(name_label, "fleetwm-greeter-login-name");
  gtk_box_append(GTK_BOX(card), name_label);

  GtkWidget* password_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_halign(password_row, GTK_ALIGN_CENTER);

  password_entry_ = gtk_password_entry_new();
  gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(password_entry_), TRUE);
  gtk_widget_add_css_class(password_entry_, "fleetwm-greeter-entry");
  gtk_box_append(GTK_BOX(password_row), password_entry_);

  unlock_button_ = gtk_button_new_from_icon_name("go-next-symbolic");
  gtk_widget_add_css_class(unlock_button_, "fleetwm-greeter-submit-button");
  gtk_widget_set_tooltip_text(unlock_button_, "Unlock");
  gtk_box_append(GTK_BOX(password_row), unlock_button_);

  gtk_box_append(GTK_BOX(card), password_row);

  error_label_ = gtk_label_new("");
  gtk_widget_add_css_class(error_label_, "fleetwm-greeter-error");
  gtk_widget_set_visible(error_label_, FALSE);
  gtk_box_append(GTK_BOX(card), error_label_);

  gtk_box_append(GTK_BOX(backdrop), card);
  gtk_window_set_child(GTK_WINDOW(window_), backdrop);

  g_signal_connect(unlock_button_, "clicked", G_CALLBACK(on_unlock_clicked), this);
  g_signal_connect(password_entry_, "activate", G_CALLBACK(on_password_activate), this);

  apply_theme();
  gtk_window_present(GTK_WINDOW(window_));
  gtk_widget_grab_focus(password_entry_);
}

void LockerWindow::apply_theme() {
  apply_app_style(theme_config_);
}

void LockerWindow::attempt_unlock() {
  const char* password = gtk_editable_get_text(GTK_EDITABLE(password_entry_));
  set_busy(true);

  if (!verify_password(username_, password != nullptr ? password : "")) {
    show_error("Incorrect password");
    return;
  }

  // send_command() appends its own trailing '\n'; best-effort like the
  // initial connect() -- if the socket dropped mid-session there is
  // nothing more graceful to do than quit anyway (see class doc comment:
  // this process being gone is itself harmless, unlike a stuck locked
  // compositor).
  if (ipc_.is_connected()) {
    ipc_.send_command("UNLOCK");
  } else {
    std::fprintf(stderr, "fleetwm-locker: not connected to compositor IPC; cannot send UNLOCK\n");
  }
  g_application_quit(g_application_get_default());
}

void LockerWindow::set_busy(bool busy) {
  gtk_widget_set_sensitive(password_entry_, !busy);
  gtk_widget_set_sensitive(unlock_button_, !busy);
}

void LockerWindow::show_error(const std::string& message) {
  gtk_label_set_text(GTK_LABEL(error_label_), message.c_str());
  gtk_widget_set_visible(error_label_, TRUE);
  gtk_editable_set_text(GTK_EDITABLE(password_entry_), "");
  set_busy(false);
  gtk_widget_grab_focus(password_entry_);
}

void LockerWindow::on_unlock_clicked(GtkButton*, gpointer user_data) {
  static_cast<LockerWindow*>(user_data)->attempt_unlock();
}

void LockerWindow::on_password_activate(GtkEntry*, gpointer user_data) {
  static_cast<LockerWindow*>(user_data)->attempt_unlock();
}

}  // namespace fleetwm::locker
