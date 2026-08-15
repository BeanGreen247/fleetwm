#include "launcher_window.hpp"

#include <fcntl.h>
#include <gtk4-layer-shell.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace fleetwm::launcher {

namespace {

constexpr int kWindowWidth = 560;
constexpr int kMaxVisibleRows = 8;
constexpr int kRowHeightPx = 48;
// Entry row + root_box margins/spacing (see build() below) on top of the
// scrolled list's own bounded height (kMaxVisibleRows * kRowHeightPx).
constexpr int kEntryAndChromeHeightPx = 60;
constexpr int kWindowHeight = kMaxVisibleRows * kRowHeightPx + kEntryAndChromeHeightPx;

GtkWidget* make_row(const std::string& primary, const std::string& secondary) {
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_margin_start(box, 10);
  gtk_widget_set_margin_end(box, 10);
  gtk_widget_set_margin_top(box, 6);
  gtk_widget_set_margin_bottom(box, 6);

  GtkWidget* primary_label = gtk_label_new(primary.c_str());
  gtk_label_set_xalign(GTK_LABEL(primary_label), 0.0f);
  PangoAttrList* attrs = pango_attr_list_new();
  pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
  gtk_label_set_attributes(GTK_LABEL(primary_label), attrs);
  pango_attr_list_unref(attrs);

  GtkWidget* secondary_label = gtk_label_new(secondary.c_str());
  gtk_label_set_xalign(GTK_LABEL(secondary_label), 0.0f);
  gtk_widget_add_css_class(secondary_label, "dim-label");

  gtk_box_append(GTK_BOX(box), primary_label);
  gtk_box_append(GTK_BOX(box), secondary_label);
  return box;
}

// First whitespace-delimited token of a typed command line, e.g. "vim
// file.txt" -> "vim". Used to look up a matching installed .desktop
// entry's Terminal= flag.
std::string first_token(const std::string& text) {
  size_t start = text.find_first_not_of(" \t");
  if (start == std::string::npos) {
    return "";
  }
  size_t end = text.find_first_of(" \t", start);
  return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// Whether an installed .desktop entry's executable basename matches
// `binary` and declares Terminal=true -- the freedesktop-spec signal
// that a command needs a terminal emulator to run in (e.g. vim, htop),
// as opposed to a GUI app that opens its own window.
bool binary_wants_terminal(const AppIndex& index, const std::string& binary) {
  if (binary.empty()) {
    return false;
  }
  for (const AppEntry& entry : index.entries()) {
    const char* exec_path = g_app_info_get_executable(G_APP_INFO(entry.info));
    if (exec_path == nullptr) {
      continue;
    }
    char* exec_basename_c = g_path_get_basename(exec_path);
    std::string exec_basename = exec_basename_c;
    g_free(exec_basename_c);
    if (exec_basename == binary) {
      return g_desktop_app_info_get_boolean(entry.info, "Terminal");
    }
  }
  return false;
}

}  // namespace

LauncherWindow::LauncherWindow(GtkApplication* app) {
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), this);
}

void LauncherWindow::on_activate(GtkApplication* app, gpointer user_data) {
  static_cast<LauncherWindow*>(user_data)->build(app);
}

void LauncherWindow::build(GtkApplication* app) {
  window_ = gtk_application_window_new(app);
  // Explicit height, not -1 ("size to content"): gtk4-layer-shell forwards
  // GTK's -1 sentinel straight through to zwlr_layer_surface_v1.set_size()
  // as (uint32_t)-1 without clamping it, and wlroots' strict protocol
  // validation (width/height must be <= INT32_MAX) fatally kills the
  // client connection on that value -- confirmed via WAYLAND_DEBUG=1 on
  // fleetwm-dev, which showed set_size(560, 4294967295) immediately
  // followed by "Lost connection to Wayland compositor".
  gtk_window_set_default_size(GTK_WINDOW(window_), kWindowWidth, kWindowHeight);
  gtk_window_set_decorated(GTK_WINDOW(window_), FALSE);

  gtk_layer_init_for_window(GTK_WINDOW(window_));
  gtk_layer_set_layer(GTK_WINDOW(window_), GTK_LAYER_SHELL_LAYER_OVERLAY);
  gtk_layer_set_keyboard_mode(GTK_WINDOW(window_), GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
  // No anchors set: unanchored layer-shell surfaces are centered by the
  // compositor, matching the Albert-style centered-popup design.

  GtkWidget* root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_margin_start(root_box, 8);
  gtk_widget_set_margin_end(root_box, 8);
  gtk_widget_set_margin_top(root_box, 8);
  gtk_widget_set_margin_bottom(root_box, 8);

  entry_ = gtk_search_entry_new();
  gtk_widget_set_hexpand(entry_, TRUE);
  g_signal_connect(entry_, "search-changed", G_CALLBACK(on_search_changed), this);
  // GtkSearchEntry (a GtkText internally) claims the Return keypress for
  // itself before it can bubble up to window_'s key controller below, so
  // Enter never reached on_key_pressed's Return case -- confirmed via
  // real testing (typing a query and pressing Enter did nothing).
  // "activate" is GtkEntry's own dedicated signal for exactly this key.
  g_signal_connect(entry_, "activate", G_CALLBACK(on_entry_activate), this);

  GtkWidget* scrolled = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request(scrolled, -1, kMaxVisibleRows * kRowHeightPx);

  list_box_ = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box_), GTK_SELECTION_BROWSE);
  g_signal_connect(list_box_, "row-activated", G_CALLBACK(on_row_activated), this);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), list_box_);

  gtk_box_append(GTK_BOX(root_box), entry_);
  gtk_box_append(GTK_BOX(root_box), scrolled);
  gtk_window_set_child(GTK_WINDOW(window_), root_box);

  GtkEventController* key_controller = gtk_event_controller_key_new();
  g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), this);
  gtk_widget_add_controller(window_, key_controller);

  refresh_results("");
  gtk_window_present(GTK_WINDOW(window_));
  gtk_widget_grab_focus(entry_);
}

void LauncherWindow::refresh_results(const std::string& query) {
  GtkWidget* child = gtk_widget_get_first_child(list_box_);
  while (child != nullptr) {
    GtkWidget* next = gtk_widget_get_next_sibling(child);
    gtk_list_box_remove(GTK_LIST_BOX(list_box_), child);
    child = next;
  }

  current_results_ = index_.search(query);

  for (const AppEntry* entry : current_results_) {
    gtk_list_box_append(GTK_LIST_BOX(list_box_), make_row(entry->name, entry->category_hint));
  }

  if (!query.empty()) {
    current_results_.push_back(nullptr);  // sentinel: run-as-command fallback
    gtk_list_box_append(GTK_LIST_BOX(list_box_), make_row(query, "Run Command"));
  }

  GtkListBoxRow* first = gtk_list_box_get_row_at_index(GTK_LIST_BOX(list_box_), 0);
  if (first != nullptr) {
    gtk_list_box_select_row(GTK_LIST_BOX(list_box_), first);
  }
}

void LauncherWindow::launch_app(GDesktopAppInfo* info) {
  GError* error = nullptr;
  if (!g_app_info_launch(G_APP_INFO(info), nullptr, nullptr, &error)) {
    std::fprintf(stderr, "fleetwm-launcher: failed to launch app: %s\n",
                 error != nullptr ? error->message : "unknown error");
    g_clear_error(&error);
  }
}

void LauncherWindow::launch_command(const std::string& text) {
  // If the typed command's binary matches an installed .desktop entry
  // declaring Terminal=true (e.g. vim, htop), run it inside foot instead
  // of directly: a bare exec would inherit fleetwm-launcher's stdio,
  // which is the raw VT fleetwm-greet runs the whole session on, so the
  // program's TUI escape codes would corrupt that console instead of
  // opening a window (confirmed via real testing). GUI commands fall
  // through to the direct-exec path below, unaffected.
  bool needs_terminal = binary_wants_terminal(index_, first_token(text));

  pid_t pid = fork();
  if (pid < 0) {
    std::fprintf(stderr, "fleetwm-launcher: fork failed: %s\n", std::strerror(errno));
    return;
  }
  if (pid == 0) {
    if (needs_terminal) {
      execlp("foot", "foot", "-e", "/bin/sh", "-c", text.c_str(), nullptr);
      _exit(1);
    }
    // Detach stdio so a GUI command that turns out to write to stdout/
    // stderr (or a CLI command not caught by the Terminal=true lookup
    // above, e.g. one with no installed .desktop entry) can't corrupt
    // the console the way the bare exec used to.
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
      dup2(devnull, STDIN_FILENO);
      dup2(devnull, STDOUT_FILENO);
      dup2(devnull, STDERR_FILENO);
    }
    execlp("/bin/sh", "sh", "-c", text.c_str(), nullptr);
    _exit(1);
  }
}

void LauncherWindow::launch_selected() {
  GtkListBoxRow* selected = gtk_list_box_get_selected_row(GTK_LIST_BOX(list_box_));
  if (selected == nullptr) {
    return;
  }
  int index = gtk_list_box_row_get_index(selected);
  if (index < 0 || static_cast<size_t>(index) >= current_results_.size()) {
    return;
  }

  const AppEntry* entry = current_results_[static_cast<size_t>(index)];
  if (entry != nullptr) {
    launch_app(entry->info);
  } else {
    const char* query = gtk_editable_get_text(GTK_EDITABLE(entry_));
    launch_command(query != nullptr ? query : "");
  }

  g_application_quit(G_APPLICATION(gtk_window_get_application(GTK_WINDOW(window_))));
}

void LauncherWindow::on_search_changed(GtkEditable* editable, gpointer user_data) {
  auto* self = static_cast<LauncherWindow*>(user_data);
  const char* text = gtk_editable_get_text(editable);
  self->refresh_results(text != nullptr ? text : "");
}

void LauncherWindow::on_row_activated(GtkListBox*, GtkListBoxRow*, gpointer user_data) {
  static_cast<LauncherWindow*>(user_data)->launch_selected();
}

void LauncherWindow::on_entry_activate(GtkEntry*, gpointer user_data) {
  static_cast<LauncherWindow*>(user_data)->launch_selected();
}

gboolean LauncherWindow::on_key_pressed(GtkEventControllerKey*, guint keyval, guint,
                                         GdkModifierType state, gpointer user_data) {
  auto* self = static_cast<LauncherWindow*>(user_data);

  if (keyval == GDK_KEY_Escape) {
    g_application_quit(G_APPLICATION(gtk_window_get_application(GTK_WINDOW(self->window_))));
    return TRUE;
  }
  if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
    self->launch_selected();
    return TRUE;
  }

  bool ctrl_held = (state & GDK_CONTROL_MASK) != 0;
  bool move_down = keyval == GDK_KEY_Down || (ctrl_held && keyval == GDK_KEY_n);
  bool move_up = keyval == GDK_KEY_Up || (ctrl_held && keyval == GDK_KEY_p);

  if (move_down || move_up) {
    GtkListBoxRow* selected = gtk_list_box_get_selected_row(GTK_LIST_BOX(self->list_box_));
    int index = selected != nullptr ? gtk_list_box_row_get_index(selected) : -1;
    index += move_down ? 1 : -1;
    GtkListBoxRow* target = gtk_list_box_get_row_at_index(GTK_LIST_BOX(self->list_box_), index);
    if (target != nullptr) {
      gtk_list_box_select_row(GTK_LIST_BOX(self->list_box_), target);
    }
    return TRUE;
  }

  return FALSE;
}

}  // namespace fleetwm::launcher
