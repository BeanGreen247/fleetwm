#include "systray.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace fleetwm::bar {

namespace {

// Only the watcher side needs an exported introspection XML -- the item
// side (org.kde.StatusNotifierItem) is something we only ever call
// methods on / read properties from via GDBusProxy, which needs no
// exported interface of its own.
constexpr const char* kWatcherIntrospectionXml = R"xml(
<node>
  <interface name="org.kde.StatusNotifierWatcher">
    <method name="RegisterStatusNotifierItem">
      <arg type="s" direction="in"/>
    </method>
    <method name="RegisterStatusNotifierHost">
      <arg type="s" direction="in"/>
    </method>
    <property name="RegisteredStatusNotifierItems" type="as" access="read"/>
    <property name="IsStatusNotifierHostRegistered" type="b" access="read"/>
    <property name="ProtocolVersion" type="i" access="read"/>
    <signal name="StatusNotifierItemRegistered"><arg type="s"/></signal>
    <signal name="StatusNotifierItemUnregistered"><arg type="s"/></signal>
    <signal name="StatusNotifierHostRegistered"/>
  </interface>
</node>
)xml";

}  // namespace

SystemTray::SystemTray(GtkWidget* tray_box) : tray_box_(tray_box) {}

SystemTray::~SystemTray() {
  for (TrayItem* item : items_) {
    if (item->watch_id != 0) {
      g_bus_unwatch_name(item->watch_id);
    }
    if (item->proxy != nullptr) {
      g_object_unref(item->proxy);
    }
    if (item->button != nullptr) {
      gtk_widget_unparent(item->button);
    }
    delete item;
  }
  if (connection_ != nullptr && registration_id_ != 0) {
    g_dbus_connection_unregister_object(connection_, registration_id_);
  }
  if (own_name_id_ != 0) {
    g_bus_unown_name(own_name_id_);
  }
}

void SystemTray::start() {
  if (own_name_id_ != 0) {
    return;
  }
  own_name_id_ = g_bus_own_name(G_BUS_TYPE_SESSION, "org.kde.StatusNotifierWatcher",
                                 G_BUS_NAME_OWNER_FLAGS_NONE, on_bus_acquired, nullptr,
                                 on_name_lost, this, nullptr);
}

void SystemTray::on_bus_acquired(GDBusConnection* connection, const char*, gpointer user_data) {
  auto* self = static_cast<SystemTray*>(user_data);
  self->connection_ = connection;

  GError* error = nullptr;
  GDBusNodeInfo* node_info = g_dbus_node_info_new_for_xml(kWatcherIntrospectionXml, &error);
  if (node_info == nullptr) {
    std::fprintf(stderr, "fleetwm-bar: systray: failed to parse introspection XML: %s\n",
                 error != nullptr ? error->message : "unknown error");
    g_clear_error(&error);
    return;
  }

  static const GDBusInterfaceVTable vtable = {
      on_method_call,
      on_get_property,
      nullptr,  // read-only interface, no set_property
  };

  self->registration_id_ = g_dbus_connection_register_object(
      connection, "/StatusNotifierWatcher", node_info->interfaces[0], &vtable, self, nullptr,
      &error);
  g_dbus_node_info_unref(node_info);
  if (self->registration_id_ == 0) {
    std::fprintf(stderr, "fleetwm-bar: systray: failed to export watcher object: %s\n",
                 error != nullptr ? error->message : "unknown error");
    g_clear_error(&error);
  }
}

void SystemTray::on_name_lost(GDBusConnection*, const char*, gpointer) {
  // Another StatusNotifierWatcher is already running (or the session bus
  // is unreachable) -- back off silently. A minimal Wayland session with
  // no DE is not expected to have one, but if it does, fighting over the
  // name would just make both implementations flicker items in and out.
  std::fprintf(stderr,
               "fleetwm-bar: systray: could not own org.kde.StatusNotifierWatcher (already "
               "taken?) -- tray icons disabled this session\n");
}

void SystemTray::on_method_call(GDBusConnection* connection, const char* sender, const char*,
                                 const char*, const char* method_name, GVariant* parameters,
                                 GDBusMethodInvocation* invocation, gpointer user_data) {
  auto* self = static_cast<SystemTray*>(user_data);

  if (std::strcmp(method_name, "RegisterStatusNotifierItem") == 0) {
    const char* param = nullptr;
    g_variant_get(parameters, "(&s)", &param);
    // The spec has never been fully consistent across implementations
    // about whether this string is an object path, a bus name, or
    // empty -- `sender` (the actual D-Bus-verified caller, not
    // client-supplied data) is always the real bus name regardless, so
    // that's what's trusted for identity; the string param is only
    // consulted for an object path if it looks like one.
    std::string object_path =
        (param != nullptr && param[0] == '/') ? param : "/StatusNotifierItem";
    self->register_item(sender, object_path);
    g_dbus_method_invocation_return_value(invocation, nullptr);
    return;
  }

  if (std::strcmp(method_name, "RegisterStatusNotifierHost") == 0) {
    // No separate host-registration bookkeeping needed -- fleetwm-bar
    // itself always *is* the host. Stub reply for spec compliance only.
    g_dbus_method_invocation_return_value(invocation, nullptr);
    return;
  }

  g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                         "Unknown method %s", method_name);
  (void)connection;
}

GVariant* SystemTray::on_get_property(GDBusConnection*, const char*, const char*, const char*,
                                       const char* property_name, GError**, gpointer user_data) {
  auto* self = static_cast<SystemTray*>(user_data);

  if (std::strcmp(property_name, "RegisteredStatusNotifierItems") == 0) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
    for (TrayItem* item : self->items_) {
      g_variant_builder_add(&builder, "s", item->bus_name.c_str());
    }
    return g_variant_builder_end(&builder);
  }
  if (std::strcmp(property_name, "IsStatusNotifierHostRegistered") == 0) {
    return g_variant_new_boolean(TRUE);
  }
  if (std::strcmp(property_name, "ProtocolVersion") == 0) {
    return g_variant_new_int32(0);
  }
  return nullptr;
}

void SystemTray::register_item(const std::string& bus_name, const std::string& object_path) {
  for (TrayItem* existing : items_) {
    if (existing->bus_name == bus_name && existing->object_path == object_path) {
      return;  // already registered -- some apps call this more than once
    }
  }

  auto* item = new TrayItem();
  item->bus_name = bus_name;
  item->object_path = object_path;
  item->self = this;
  items_.push_back(item);

  item->watch_id = g_bus_watch_name(G_BUS_TYPE_SESSION, bus_name.c_str(),
                                     G_BUS_NAME_WATCHER_FLAGS_NONE, nullptr,
                                     on_item_owner_vanished, item, nullptr);

  g_dbus_proxy_new_for_bus(G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, nullptr,
                            bus_name.c_str(), object_path.c_str(), "org.kde.StatusNotifierItem",
                            nullptr, on_item_proxy_ready, item);

  if (connection_ != nullptr) {
    g_dbus_connection_emit_signal(connection_, nullptr, "/StatusNotifierWatcher",
                                   "org.kde.StatusNotifierWatcher", "StatusNotifierItemRegistered",
                                   g_variant_new("(s)", bus_name.c_str()), nullptr);
  }
}

void SystemTray::unregister_item(const std::string& bus_name) {
  auto it = std::find_if(items_.begin(), items_.end(),
                          [&bus_name](TrayItem* item) { return item->bus_name == bus_name; });
  if (it == items_.end()) {
    return;
  }
  TrayItem* item = *it;
  items_.erase(it);

  if (item->watch_id != 0) {
    g_bus_unwatch_name(item->watch_id);
  }
  if (item->proxy != nullptr) {
    g_object_unref(item->proxy);
  }
  if (item->button != nullptr) {
    gtk_widget_unparent(item->button);
  }

  if (connection_ != nullptr) {
    g_dbus_connection_emit_signal(
        connection_, nullptr, "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
        "StatusNotifierItemUnregistered", g_variant_new("(s)", bus_name.c_str()), nullptr);
  }
  delete item;
}

void SystemTray::on_item_owner_vanished(GDBusConnection*, const char*, gpointer user_data) {
  auto* item = static_cast<TrayItem*>(user_data);
  // item may already be mid-destruction if unregister_item() was reached
  // some other way first; self->unregister_item() below looks it up
  // fresh by bus_name and no-ops if it's already gone, so this is safe
  // either way.
  item->self->unregister_item(item->bus_name);
}

void SystemTray::on_item_proxy_ready(GObject*, GAsyncResult* result, gpointer user_data) {
  auto* item = static_cast<TrayItem*>(user_data);
  GError* error = nullptr;
  GDBusProxy* proxy = g_dbus_proxy_new_for_bus_finish(result, &error);
  if (proxy == nullptr) {
    std::fprintf(stderr, "fleetwm-bar: systray: failed to create proxy for %s: %s\n",
                 item->bus_name.c_str(), error != nullptr ? error->message : "unknown error");
    g_clear_error(&error);
    // The item was already added to items_/tray_box_ bookkeeping in
    // register_item() -- leave it tracked but iconless rather than
    // partially unwinding; on_item_owner_vanished will clean it up if
    // the process goes away, same as any other item.
    return;
  }
  item->proxy = proxy;
  g_signal_connect(proxy, "g-properties-changed", G_CALLBACK(on_item_properties_changed), item);

  GtkWidget* icon = gtk_image_new();
  gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
  gtk_widget_set_cursor_from_name(icon, "pointer");
  gtk_widget_add_css_class(icon, "fleetwm-tray-icon");

  GtkGesture* click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);  // 0 = any button
  g_signal_connect(click, "pressed", G_CALLBACK(on_item_clicked), item);
  gtk_widget_add_controller(icon, GTK_EVENT_CONTROLLER(click));

  gtk_box_append(GTK_BOX(item->self->tray_box_), icon);
  item->button = icon;

  item->self->refresh_item_icon(item);
}

void SystemTray::on_item_properties_changed(GDBusProxy*, GVariant* changed_properties, GStrv,
                                             gpointer user_data) {
  auto* item = static_cast<TrayItem*>(user_data);
  // Only actually re-render on the properties that affect the icon --
  // Status/ToolTip/Title changes (also delivered via this same signal)
  // don't need a re-render, and refresh_item_icon() re-reads from the
  // proxy's own already-updated property cache regardless of which
  // property triggered this.
  if (g_variant_lookup_value(changed_properties, "IconPixmap", nullptr) != nullptr ||
      g_variant_lookup_value(changed_properties, "IconName", nullptr) != nullptr) {
    item->self->refresh_item_icon(item);
  }
}

void SystemTray::refresh_item_icon(TrayItem* item) {
  if (item->proxy == nullptr || item->button == nullptr) {
    return;
  }

  // Prefer the raw IconPixmap over an IconName-based icon-theme lookup --
  // guaranteed correct regardless of whether the running icon theme
  // happens to have a matching name, which is exactly the kind of thing
  // that silently breaks Steam/Discord's tray icon on a minimal desktop
  // with no full icon theme installed.
  GVariant* pixmaps = g_dbus_proxy_get_cached_property(item->proxy, "IconPixmap");
  if (pixmaps != nullptr) {
    GVariantIter iter;
    g_variant_iter_init(&iter, pixmaps);
    gint32 best_w = 0, best_h = 0;
    GVariant* best_data = nullptr;
    gint32 w, h;
    GVariant* data;
    while (g_variant_iter_next(&iter, "(ii@ay)", &w, &h, &data)) {
      if (w > 0 && h > 0 && static_cast<gint64>(w) * h > static_cast<gint64>(best_w) * best_h) {
        if (best_data != nullptr) {
          g_variant_unref(best_data);
        }
        best_w = w;
        best_h = h;
        best_data = data;
      } else {
        g_variant_unref(data);
      }
    }
    if (best_data != nullptr) {
      gsize len = 0;
      const void* bytes_ptr = g_variant_get_fixed_array(best_data, &len, sizeof(guint8));
      if (len >= static_cast<gsize>(best_w) * best_h * 4) {
        GBytes* bytes = g_bytes_new(bytes_ptr, len);
        // SNI IconPixmap is ARGB32 in network (big-endian) byte order --
        // GDK_MEMORY_A8R8G8B8 names its byte layout the same way (first
        // byte alpha, then red, green, blue), so no byte-swapping needed.
        GdkTexture* texture =
            gdk_memory_texture_new(best_w, best_h, GDK_MEMORY_A8R8G8B8, bytes, best_w * 4);
        gtk_image_set_from_paintable(GTK_IMAGE(item->button), GDK_PAINTABLE(texture));
        g_object_unref(texture);
        g_bytes_unref(bytes);
        g_variant_unref(best_data);
        g_variant_unref(pixmaps);
        return;
      }
      g_variant_unref(best_data);
    }
    g_variant_unref(pixmaps);
  }

  GVariant* icon_name_v = g_dbus_proxy_get_cached_property(item->proxy, "IconName");
  if (icon_name_v != nullptr) {
    const char* icon_name = g_variant_get_string(icon_name_v, nullptr);
    if (icon_name != nullptr && icon_name[0] != '\0') {
      gtk_image_set_from_icon_name(GTK_IMAGE(item->button), icon_name);
      g_variant_unref(icon_name_v);
      return;
    }
    g_variant_unref(icon_name_v);
  }

  // Neither a usable pixmap nor a name -- generic fallback rather than a
  // blank/invisible icon, so a misbehaving item is still visible (and
  // clickable) in the tray.
  gtk_image_set_from_icon_name(GTK_IMAGE(item->button), "application-x-executable-symbolic");
}

void SystemTray::on_item_clicked(GtkGestureClick* gesture, int, double, double,
                                  gpointer user_data) {
  auto* item = static_cast<TrayItem*>(user_data);
  if (item->proxy == nullptr) {
    return;
  }
  guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
  const char* method = (button == GDK_BUTTON_SECONDARY) ? "ContextMenu" : "Activate";
  // Fire-and-forget: Activate/ContextMenu have no meaningful reply, and
  // blocking a click on a D-Bus round-trip would make the tray feel
  // laggy for no benefit.
  g_dbus_proxy_call(item->proxy, method, g_variant_new("(ii)", 0, 0), G_DBUS_CALL_FLAGS_NONE, -1,
                     nullptr, nullptr, nullptr);
}

}  // namespace fleetwm::bar
