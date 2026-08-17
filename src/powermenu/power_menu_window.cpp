#include "power_menu_window.hpp"

#include <gtk4-layer-shell.h>
#include <systemd/sd-login.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace fleetwm::powermenu {

namespace {

struct ActionSpec {
  const char* name;
  const char* label;
  const char* icon;
};

constexpr ActionSpec kActions[] = {
    {"lock", "Lock", "system-lock-screen-symbolic"},
    {"logout", "Log out", "system-log-out-symbolic"},
    {"sleep", "Sleep", "weather-clear-night-symbolic"},
    {"reboot", "Reboot", "view-refresh-symbolic"},
    {"shutdown", "Shut down", "system-shutdown-symbolic"},
};

}  // namespace

PowerMenuWindow::PowerMenuWindow(GtkApplication* app) {
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), this);
}

void PowerMenuWindow::on_activate(GtkApplication* app, gpointer user_data) {
  static_cast<PowerMenuWindow*>(user_data)->build(app);
}

void PowerMenuWindow::build(GtkApplication* app) {
  theme_config_ = load_theme_config();
  ipc_.connect();  // best-effort, same as the locker -- only needed for "lock"

  window_ = gtk_application_window_new(app);
  gtk_window_set_decorated(GTK_WINDOW(window_), FALSE);
  gtk_widget_add_css_class(window_, "fleetwm-panel");

  gtk_layer_init_for_window(GTK_WINDOW(window_));
  gtk_layer_set_layer(GTK_WINDOW(window_), GTK_LAYER_SHELL_LAYER_OVERLAY);
  gtk_layer_set_keyboard_mode(GTK_WINDOW(window_), GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
  // Full-screen anchor, same as the locker -- a plain top-level layer
  // surface (not a popup), which is the whole point: this input path is
  // already proven reliable by the locker's own unlock button.
  gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);

  GtkWidget* backdrop = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_halign(backdrop, GTK_ALIGN_FILL);
  gtk_widget_set_valign(backdrop, GTK_ALIGN_FILL);

  card_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_add_css_class(card_, "fleetwm-powermenu-card");
  gtk_widget_set_halign(card_, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(card_, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand(card_, TRUE);
  gtk_widget_set_vexpand(card_, TRUE);

  for (const ActionSpec& action : kActions) {
    GtkWidget* button = gtk_button_new();
    gtk_widget_add_css_class(button, "fleetwm-powermenu-button");
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* icon = gtk_image_new_from_icon_name(action.icon);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 20);
    gtk_box_append(GTK_BOX(content), icon);
    gtk_box_append(GTK_BOX(content), gtk_label_new(action.label));
    gtk_button_set_child(GTK_BUTTON(button), content);
    g_object_set_data(G_OBJECT(button), "fleetwm-action", const_cast<char*>(action.name));
    g_signal_connect(button, "clicked", G_CALLBACK(on_action_clicked), this);
    gtk_box_append(GTK_BOX(card_), button);
  }

  gtk_box_append(GTK_BOX(backdrop), card_);
  gtk_window_set_child(GTK_WINDOW(window_), backdrop);

  // Clicking the backdrop itself (not a button) closes without acting.
  // Attached to the whole window (not just the empty backdrop area) and
  // gated by an explicit hit-test in on_backdrop_clicked -- GTK4's click
  // gesture propagation does NOT reliably stop a "released" handler on an
  // ancestor from also firing when a descendant button already claimed
  // and handled the same click (confirmed by testing: every button click
  // also silently closed the window before its own action ran), so
  // closing must be conditioned on the pick result rather than trusting
  // gesture-claim propagation alone.
  GtkGesture* backdrop_click = gtk_gesture_click_new();
  gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(backdrop_click),
                                              GTK_PHASE_BUBBLE);
  g_signal_connect(backdrop_click, "released", G_CALLBACK(on_backdrop_clicked), this);
  gtk_widget_add_controller(window_, GTK_EVENT_CONTROLLER(backdrop_click));

  GtkEventController* key_controller = gtk_event_controller_key_new();
  g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), this);
  gtk_widget_add_controller(window_, key_controller);

  apply_theme();
  gtk_window_present(GTK_WINDOW(window_));
}

void PowerMenuWindow::apply_theme() {
  apply_app_style(theme_config_);
}

void PowerMenuWindow::close() {
  g_application_quit(g_application_get_default());
}

void PowerMenuWindow::run_action(const char* name) {
  if (std::strcmp(name, "lock") == 0) {
    // Same as the bar's old Lock handling: goes over the compositor IPC
    // socket, which is what actually spawns fleetwm-locker.
    if (ipc_.is_connected()) {
      ipc_.send_command("LOCK");
    } else {
      std::fprintf(stderr, "fleetwm-powermenu: not connected to compositor IPC; cannot lock\n");
    }
    close();
    return;
  }

  // Built directly as owned, heap-duplicated strings (g_strdup at the
  // point each piece is known) rather than through an intermediate
  // const char* argv[] of borrowed pointers -- an earlier version of
  // this function assembled a local const char*[] first and only
  // g_strdup'd everything in a second pass right before g_spawn_async,
  // and the session-id slot (borrowed from a std::string's SSO buffer)
  // was reliably nullptr by the time that second pass read it back,
  // despite being valid immediately after assignment (reproduced
  // consistently on fleetwm-dev's LTO+-march=native release build).
  // Duplicating each piece immediately, with nothing borrowed across
  // statements, sidesteps that lifetime hazard entirely.
  std::vector<char*> child_argv;

  if (std::strcmp(name, "logout") == 0) {
    // sd_pid_get_session(), not getenv("XDG_SESSION_ID") -- the latter is
    // never actually set in this process's environment (modern
    // pam_systemd(8) no longer exports it via the PAM environment list),
    // which is why "Log out" silently did nothing when the bar's old
    // popover tried it that way.
    char* session_id_raw = nullptr;
    if (sd_pid_get_session(getpid(), &session_id_raw) < 0 || session_id_raw == nullptr) {
      std::fprintf(stderr, "fleetwm-powermenu: logout failed: could not determine session id\n");
      close();
      return;
    }
    child_argv.push_back(g_strdup("loginctl"));
    child_argv.push_back(g_strdup("terminate-session"));
    child_argv.push_back(g_strdup(session_id_raw));
    std::free(session_id_raw);
  } else if (std::strcmp(name, "sleep") == 0) {
    child_argv.push_back(g_strdup("systemctl"));
    child_argv.push_back(g_strdup("suspend"));
  } else if (std::strcmp(name, "reboot") == 0) {
    child_argv.push_back(g_strdup("systemctl"));
    child_argv.push_back(g_strdup("reboot"));
  } else if (std::strcmp(name, "shutdown") == 0) {
    child_argv.push_back(g_strdup("systemctl"));
    child_argv.push_back(g_strdup("poweroff"));
  } else {
    close();
    return;
  }
  child_argv.push_back(nullptr);  // g_spawn_async requires NULL-terminated argv

  GError* error = nullptr;
  if (!g_spawn_async(nullptr, child_argv.data(), nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr,
                      nullptr, &error)) {
    std::fprintf(stderr, "fleetwm-powermenu: action '%s' failed: %s\n", name,
                 error != nullptr ? error->message : "unknown error");
    g_clear_error(&error);
  }
  // Free each g_strdup'd string individually, not via g_strfreev() -- that
  // also frees the array pointer itself via g_free(), but child_argv.data()
  // is a std::vector's own internal buffer, not a GLib-owned array; the
  // vector already frees that buffer itself when it goes out of scope
  // below, so g_strfreev() here was a double free (confirmed by a real
  // "double free detected in tcache" glibc abort during testing).
  for (char* arg : child_argv) {
    g_free(arg);
  }
  close();
}

void PowerMenuWindow::on_action_clicked(GtkButton* button, gpointer user_data) {
  const char* name = static_cast<const char*>(g_object_get_data(G_OBJECT(button), "fleetwm-action"));
  static_cast<PowerMenuWindow*>(user_data)->run_action(name);
}

void PowerMenuWindow::on_backdrop_clicked(GtkGestureClick*, int, double x, double y,
                                           gpointer user_data) {
  auto* self = static_cast<PowerMenuWindow*>(user_data);
  // x,y are relative to window_ (the widget the controller is attached
  // to). gtk_widget_pick() finds the actual topmost widget under that
  // point; only close if it's the card itself or NOT a descendant of it
  // (a button click already handled itself via on_action_clicked, so this
  // must not also close the window out from under it).
  GtkWidget* hit = gtk_widget_pick(self->window_, x, y, GTK_PICK_DEFAULT);
  while (hit != nullptr) {
    if (hit == self->card_) {
      return;  // inside the card (a button or its label/icon) -- ignore
    }
    hit = gtk_widget_get_parent(hit);
  }
  self->close();
}

gboolean PowerMenuWindow::on_key_pressed(GtkEventControllerKey*, guint keyval, guint,
                                          GdkModifierType, gpointer user_data) {
  if (keyval == GDK_KEY_Escape) {
    static_cast<PowerMenuWindow*>(user_data)->close();
    return TRUE;
  }
  return FALSE;
}

}  // namespace fleetwm::powermenu
