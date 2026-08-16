#include "login_window.hpp"

#include <glib-unix.h>
#include <pwd.h>

#include <algorithm>

#include "login_ipc.hpp"

namespace fleetwm::greeter_login {

namespace {

// Real login-capable local accounts: UID >= 1000 (the conventional
// "regular user" floor -- matches useradd/login.defs' own UID_MIN
// default on Debian/Ubuntu) excluding "nobody" (65534), with a real
// shell (not one of the standard nologin/false variants). Service/system
// accounts created by packages (UID < 1000, or UID_MIN..nobody with a
// nologin shell) never show up as picker tiles. root is excluded by the
// UID >= 1000 floor alone, but see also main.cpp's own hard rejection of
// username == "root" on the LoginAttempt path -- that is the real
// enforcement point (root login is graphical-greeter policy: root
// should only ever be a deliberate terminal/rescue-shell login, never a
// routine desktop session), this filter is just what keeps root off the
// picker's UI in the first place.
std::vector<std::string> list_login_users() {
  std::vector<std::string> users;
  setpwent();
  while (passwd* pw = getpwent()) {
    if (pw->pw_uid < 1000 || pw->pw_uid == 65534) {
      continue;
    }
    if (pw->pw_shell == nullptr || pw->pw_shell[0] == '\0') {
      continue;
    }
    std::string shell = pw->pw_shell;
    if (shell == "/usr/sbin/nologin" || shell == "/sbin/nologin" || shell == "/bin/false" ||
        shell == "/usr/bin/false") {
      continue;
    }
    users.emplace_back(pw->pw_name);
  }
  endpwent();
  std::sort(users.begin(), users.end());
  return users;
}

}  // namespace

LoginWindow::LoginWindow(GtkApplication* app, int ipc_fd) : ipc_fd_(ipc_fd) {
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), this);
}

void LoginWindow::on_activate(GtkApplication* app, gpointer user_data) {
  static_cast<LoginWindow*>(user_data)->build(app);
}

void LoginWindow::build(GtkApplication* app) {
  // Read fresh on every greeter startup, not cached -- a theme/accent
  // change made in fleetwm-settings during the previous session shows up
  // on the very next login screen with no separate greeter theme config
  // to keep in sync (explicit product requirement).
  theme_config_ = load_theme_config();
  wallpaper_config_ = load_wallpaper_config();
  apply_app_style(theme_config_);
  login_users_ = list_login_users();

  window_ = gtk_application_window_new(app);
  gtk_window_set_decorated(GTK_WINDOW(window_), FALSE);
  gtk_widget_add_css_class(window_, "fleetwm-panel");  // bg_primary fallback behind the picture

  GtkWidget* overlay = gtk_overlay_new();
  gtk_window_set_child(GTK_WINDOW(window_), overlay);

  GtkWidget* background = gtk_picture_new();
  gtk_picture_set_content_fit(GTK_PICTURE(background), GTK_CONTENT_FIT_COVER);
  gtk_widget_set_hexpand(background, TRUE);
  gtk_widget_set_vexpand(background, TRUE);
  if (!wallpaper_config_.use_solid_color && !wallpaper_config_.path.empty()) {
    gtk_picture_set_filename(GTK_PICTURE(background), wallpaper_config_.path.c_str());
  }
  gtk_overlay_set_child(GTK_OVERLAY(overlay), background);

  stack_ = gtk_stack_new();
  // Without this, GtkStack's default homogeneous sizing keeps the stack
  // always as large as its *tallest* page even while showing the
  // shorter one -- the picker page (short) then centers inside that
  // oversized allocation, showing as a large, uneven gap before the
  // brand/power row below it that isn't part of the page's own spacing
  // at all. Confirmed via a real screenshot.
  gtk_stack_set_hhomogeneous(GTK_STACK(stack_), FALSE);
  gtk_stack_set_vhomogeneous(GTK_STACK(stack_), FALSE);
  gtk_widget_set_halign(stack_, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(stack_, GTK_ALIGN_CENTER);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), stack_);

  gtk_stack_add_named(GTK_STACK(stack_), build_picker_page(), "picker");
  gtk_stack_add_named(GTK_STACK(stack_), build_login_page(), "login");

  g_unix_fd_add(ipc_fd_, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR),
                on_ipc_readable, this);

  gtk_window_present(GTK_WINDOW(window_));

  if (!login_users_.empty()) {
    show_picker_page();
  } else {
    show_login_page("");
  }
}

GtkWidget* LoginWindow::build_power_row() {
  GtkWidget* power_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(power_row, GTK_ALIGN_CENTER);

  GtkWidget* reboot_button = gtk_button_new_from_icon_name("system-reboot-symbolic");
  gtk_widget_add_css_class(reboot_button, "fleetwm-greeter-power-button");
  gtk_widget_set_tooltip_text(reboot_button, "Restart");
  g_signal_connect_swapped(reboot_button, "clicked",
                            G_CALLBACK(+[](gpointer user_data) {
                              static_cast<LoginWindow*>(user_data)->request_power_action("reboot");
                            }),
                            this);
  gtk_box_append(GTK_BOX(power_row), reboot_button);

  GtkWidget* poweroff_button = gtk_button_new_from_icon_name("system-shutdown-symbolic");
  gtk_widget_add_css_class(poweroff_button, "fleetwm-greeter-power-button");
  gtk_widget_set_tooltip_text(poweroff_button, "Shut Down");
  g_signal_connect_swapped(
      poweroff_button, "clicked",
      G_CALLBACK(+[](gpointer user_data) {
        static_cast<LoginWindow*>(user_data)->request_power_action("poweroff");
      }),
      this);
  gtk_box_append(GTK_BOX(power_row), poweroff_button);

  return power_row;
}

// A plain square avatar frame -- there is no per-user profile-picture
// store anywhere in fleetwm, so every tile/login-page avatar uses the
// same generic person glyph, just at different sizes (picker tiles
// smaller than the single-user login page's, matching the Windows 7
// reference this whole picker is modeled on).
GtkWidget* LoginWindow::build_avatar(int size) {
  GtkWidget* frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(frame, "fleetwm-greeter-avatar");
  gtk_widget_set_size_request(frame, size, size);
  gtk_widget_set_halign(frame, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(frame, GTK_ALIGN_CENTER);

  GtkWidget* icon = gtk_image_new_from_icon_name("avatar-default-symbolic");
  gtk_image_set_pixel_size(GTK_IMAGE(icon), size * 2 / 3);
  gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand(icon, TRUE);
  gtk_widget_set_vexpand(icon, TRUE);
  gtk_box_append(GTK_BOX(frame), icon);

  return frame;
}

GtkWidget* LoginWindow::build_user_tile(const std::string& username) {
  GtkWidget* button = gtk_button_new();
  gtk_widget_add_css_class(button, "fleetwm-greeter-tile-button");
  // Without this, GtkBox's default GTK_ALIGN_FILL cross-axis alignment
  // stretches every button in the row to match the row's tallest natural
  // height, leaving a large empty gap between each tile's avatar and its
  // name label -- confirmed via a real screenshot.
  gtk_widget_set_valign(button, GTK_ALIGN_START);

  GtkWidget* column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_box_append(GTK_BOX(column), build_avatar(88));

  GtkWidget* label = gtk_label_new(username.empty() ? "Other User" : username.c_str());
  gtk_widget_add_css_class(label, "fleetwm-greeter-tile-name");
  gtk_box_append(GTK_BOX(column), label);

  gtk_button_set_child(GTK_BUTTON(button), column);

  // Username travels with the button via g_object data (freed when the
  // button is destroyed) rather than a per-tile lambda capture, since
  // GTK's C signal API needs a plain function pointer here.
  g_object_set_data_full(G_OBJECT(button), "fleetwm-username", new std::string(username),
                          [](gpointer p) { delete static_cast<std::string*>(p); });
  g_signal_connect(button, "clicked",
                    G_CALLBACK(+[](GtkButton* btn, gpointer user_data) {
                      auto* self = static_cast<LoginWindow*>(user_data);
                      auto* uname =
                          static_cast<std::string*>(g_object_get_data(G_OBJECT(btn), "fleetwm-username"));
                      self->show_login_page(*uname);
                    }),
                    this);

  return button;
}

GtkWidget* LoginWindow::build_picker_page() {
  // No card/box chrome -- tiles float directly on the wallpaper, same as
  // the Windows 7 welcome screen this is modeled on.
  GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);

  GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 28);
  gtk_widget_set_halign(row, GTK_ALIGN_CENTER);
  for (const std::string& username : login_users_) {
    gtk_box_append(GTK_BOX(row), build_user_tile(username));
  }
  gtk_box_append(GTK_BOX(row), build_user_tile(""));  // "" == Other User
  gtk_box_append(GTK_BOX(page), row);

  GtkWidget* brand = gtk_label_new("fleetwm");
  gtk_widget_add_css_class(brand, "fleetwm-greeter-brand");
  gtk_box_append(GTK_BOX(page), brand);

  gtk_box_append(GTK_BOX(page), build_power_row());
  return page;
}

GtkWidget* LoginWindow::build_login_page() {
  GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_halign(page, GTK_ALIGN_CENTER);

  // Only meaningful (and only ever shown) when a picker actually exists
  // -- show_login_page() hides it entirely on a picker-less login.
  GtkWidget* back = gtk_button_new_from_icon_name("go-previous-symbolic");
  gtk_widget_add_css_class(back, "fleetwm-greeter-back-button");
  gtk_widget_set_tooltip_text(back, "Switch User");
  gtk_widget_set_halign(back, GTK_ALIGN_START);
  g_signal_connect_swapped(
      back, "clicked",
      G_CALLBACK(+[](gpointer user_data) { static_cast<LoginWindow*>(user_data)->show_picker_page(); }),
      this);
  back_button_ = back;
  gtk_box_append(GTK_BOX(page), back);

  login_avatar_box_ = build_avatar(128);
  gtk_box_append(GTK_BOX(page), login_avatar_box_);

  login_name_label_ = gtk_label_new("");
  gtk_widget_add_css_class(login_name_label_, "fleetwm-greeter-login-name");
  gtk_box_append(GTK_BOX(page), login_name_label_);

  username_entry_ = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(username_entry_), "Username");
  gtk_widget_add_css_class(username_entry_, "fleetwm-greeter-entry");
  gtk_widget_set_halign(username_entry_, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(page), username_entry_);

  GtkWidget* password_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_halign(password_row, GTK_ALIGN_CENTER);

  password_entry_ = gtk_password_entry_new();
  gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(password_entry_), TRUE);
  gtk_widget_add_css_class(password_entry_, "fleetwm-greeter-entry");
  gtk_box_append(GTK_BOX(password_row), password_entry_);

  login_button_ = gtk_button_new_from_icon_name("go-next-symbolic");
  gtk_widget_add_css_class(login_button_, "fleetwm-greeter-submit-button");
  gtk_widget_set_tooltip_text(login_button_, "Log In");
  gtk_box_append(GTK_BOX(password_row), login_button_);

  gtk_box_append(GTK_BOX(page), password_row);

  // Forces the username field to the same width as the password-field +
  // submit-button row below it -- explicit request; GtkSizeGroup is the
  // standard GTK mechanism for matching sibling widget widths, since
  // plain CSS min-width would need the two rows' combined width
  // hardcoded and kept in sync by hand.
  GtkSizeGroup* width_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
  gtk_size_group_add_widget(width_group, username_entry_);
  gtk_size_group_add_widget(width_group, password_row);
  g_object_unref(width_group);  // the widgets themselves hold the group alive

  error_label_ = gtk_label_new("");
  gtk_widget_add_css_class(error_label_, "fleetwm-greeter-error");
  gtk_widget_set_visible(error_label_, FALSE);
  gtk_box_append(GTK_BOX(page), error_label_);

  GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_size_request(spacer, -1, 12);
  gtk_box_append(GTK_BOX(page), spacer);
  gtk_box_append(GTK_BOX(page), build_power_row());

  g_signal_connect(login_button_, "clicked", G_CALLBACK(on_login_clicked), this);
  g_signal_connect(password_entry_, "activate", G_CALLBACK(on_password_activate), this);

  return page;
}

void LoginWindow::show_picker_page() {
  showing_picker_ = true;
  gtk_stack_set_visible_child_name(GTK_STACK(stack_), "picker");
}

void LoginWindow::show_login_page(const std::string& username) {
  showing_picker_ = false;

  bool locked = !username.empty();
  gtk_widget_set_visible(login_name_label_, locked);
  gtk_widget_set_visible(username_entry_, !locked);
  if (locked) {
    gtk_label_set_text(GTK_LABEL(login_name_label_), username.c_str());
  } else {
    gtk_editable_set_text(GTK_EDITABLE(username_entry_), "");
  }

  gtk_editable_set_text(GTK_EDITABLE(password_entry_), "");
  gtk_widget_set_visible(error_label_, FALSE);
  gtk_widget_set_visible(back_button_, !login_users_.empty());

  gtk_stack_set_visible_child_name(GTK_STACK(stack_), "login");
  gtk_widget_grab_focus(locked ? password_entry_ : username_entry_);
}

void LoginWindow::attempt_login() {
  const char* username = gtk_widget_get_visible(username_entry_)
                              ? gtk_editable_get_text(GTK_EDITABLE(username_entry_))
                              : gtk_label_get_text(GTK_LABEL(login_name_label_));
  const char* password = gtk_editable_get_text(GTK_EDITABLE(password_entry_));
  if (username == nullptr || username[0] == '\0') {
    return;
  }
  set_busy(true);
  greeter_ipc::send_login_attempt(ipc_fd_, username, password ? password : "");
}

void LoginWindow::request_power_action(const char* action) {
  greeter_ipc::send_power_action(ipc_fd_, action);
}

void LoginWindow::set_busy(bool busy) {
  gtk_widget_set_sensitive(username_entry_, !busy);
  gtk_widget_set_sensitive(password_entry_, !busy);
  gtk_widget_set_sensitive(login_button_, !busy);
}

void LoginWindow::show_error(const std::string& message) {
  gtk_label_set_text(GTK_LABEL(error_label_), message.c_str());
  gtk_widget_set_visible(error_label_, TRUE);
  gtk_editable_set_text(GTK_EDITABLE(password_entry_), "");
  set_busy(false);
  gtk_widget_grab_focus(password_entry_);
}

void LoginWindow::on_login_clicked(GtkButton*, gpointer user_data) {
  static_cast<LoginWindow*>(user_data)->attempt_login();
}

void LoginWindow::on_password_activate(GtkEntry*, gpointer user_data) {
  static_cast<LoginWindow*>(user_data)->attempt_login();
}

gboolean LoginWindow::on_ipc_readable(gint fd, GIOCondition condition, gpointer user_data) {
  auto* self = static_cast<LoginWindow*>(user_data);
  if (condition & (G_IO_HUP | G_IO_ERR)) {
    // fleetwm-greet is tearing this process down (either the login
    // succeeded, or it gave up on this cycle) -- nothing to render, the
    // process is about to be SIGTERM'd.
    return G_SOURCE_REMOVE;
  }
  greeter_ipc::ServerMessage msg;
  if (!greeter_ipc::recv_server_message(fd, msg)) {
    return G_SOURCE_REMOVE;
  }
  if (msg.type == greeter_ipc::ServerMsgType::AuthFailed) {
    self->show_error(msg.message);
  }
  return G_SOURCE_CONTINUE;
}

}  // namespace fleetwm::greeter_login
