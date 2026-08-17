#pragma once

#include <gio/gio.h>
#include <gtk/gtk.h>

#include <string>
#include <vector>

namespace fleetwm::bar {

// Implements both halves of the StatusNotifierItem/StatusNotifierWatcher
// spec (freedesktop.org's de-facto systray standard, originally from
// KDE -- what Steam/Discord/etc. actually speak on Linux; there is no
// competing standard worth also supporting) on the session bus:
//
//  - Watcher: owns the well-known name "org.kde.StatusNotifierWatcher"
//    and exports a /StatusNotifierWatcher object implementing
//    RegisterStatusNotifierItem -- this is the registry apps look up and
//    register themselves with. No other watcher is expected to already
//    be running on a minimal Wayland session with no DE, but if one is
//    (name ownership lost), this backs off silently rather than fighting
//    over the name.
//  - Host: for each registered item, creates a GDBusProxy to its own
//    org.kde.StatusNotifierItem interface, renders an icon button in the
//    tray box (preferring the item's raw IconPixmap over IconName-based
//    icon-theme lookup, since the pixmap is guaranteed correct
//    regardless of whether the running icon theme happens to have that
//    name), and forwards clicks to Activate()/ContextMenu() on the item.
//
// Owned by BarWindow, constructed with the GtkBox icons should be
// appended into (already placed in the bar's right-side box).
class SystemTray {
 public:
  explicit SystemTray(GtkWidget* tray_box);
  ~SystemTray();

  SystemTray(const SystemTray&) = delete;
  SystemTray& operator=(const SystemTray&) = delete;

  // Owns org.kde.StatusNotifierWatcher on the session bus and starts
  // accepting registrations. Safe to call once; a second call is a
  // no-op guard, not expected to ever actually happen (BarWindow calls
  // this once from build()).
  void start();

 private:
  struct TrayItem {
    std::string bus_name;
    std::string object_path;
    GDBusProxy* proxy = nullptr;
    GtkWidget* button = nullptr;
    guint watch_id = 0;  // g_bus_watch_name, detects the owner vanishing
    SystemTray* self = nullptr;
  };

  static void on_bus_acquired(GDBusConnection* connection, const char* name, gpointer user_data);
  static void on_name_lost(GDBusConnection* connection, const char* name, gpointer user_data);

  static void on_method_call(GDBusConnection* connection, const char* sender,
                              const char* object_path, const char* interface_name,
                              const char* method_name, GVariant* parameters,
                              GDBusMethodInvocation* invocation, gpointer user_data);
  static GVariant* on_get_property(GDBusConnection* connection, const char* sender,
                                    const char* object_path, const char* interface_name,
                                    const char* property_name, GError** error,
                                    gpointer user_data);

  void register_item(const std::string& bus_name, const std::string& object_path);
  void unregister_item(const std::string& bus_name);

  static void on_item_proxy_ready(GObject* source, GAsyncResult* result, gpointer user_data);
  static void on_item_properties_changed(GDBusProxy* proxy, GVariant* changed_properties,
                                          GStrv invalidated_properties, gpointer user_data);
  static void on_item_owner_vanished(GDBusConnection* connection, const char* name,
                                      gpointer user_data);

  void refresh_item_icon(TrayItem* item);
  static void on_item_clicked(GtkGestureClick* gesture, int n_press, double x, double y,
                               gpointer user_data);

  GtkWidget* tray_box_;
  GDBusConnection* connection_ = nullptr;
  guint own_name_id_ = 0;
  guint registration_id_ = 0;
  std::vector<TrayItem*> items_;
};

}  // namespace fleetwm::bar
