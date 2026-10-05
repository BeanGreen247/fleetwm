/* sd-bus vtable for org.kde.StatusNotifierWatcher. Lives in a C file because
 * the SD_BUS_* macros use C99 designated initializers that C++ rejects; the
 * handlers are implemented in tray.cpp (extern "C"). */
#include <systemd/sd-bus.h>

extern int fleetwm_tray_register_item(sd_bus_message*, void*, sd_bus_error*);
extern int fleetwm_tray_register_host(sd_bus_message*, void*, sd_bus_error*);
extern int fleetwm_tray_prop_items(sd_bus*, const char*, const char*, const char*,
                                   sd_bus_message*, void*, sd_bus_error*);
extern int fleetwm_tray_prop_host(sd_bus*, const char*, const char*, const char*,
                                  sd_bus_message*, void*, sd_bus_error*);
extern int fleetwm_tray_prop_version(sd_bus*, const char*, const char*, const char*,
                                     sd_bus_message*, void*, sd_bus_error*);

const sd_bus_vtable fleetwm_watcher_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("RegisterStatusNotifierItem", "s", "", fleetwm_tray_register_item,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RegisterStatusNotifierHost", "s", "", fleetwm_tray_register_host,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("RegisteredStatusNotifierItems", "as", fleetwm_tray_prop_items, 0, 0),
    SD_BUS_PROPERTY("IsStatusNotifierHostRegistered", "b", fleetwm_tray_prop_host, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ProtocolVersion", "i", fleetwm_tray_prop_version, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_SIGNAL("StatusNotifierItemRegistered", "s", 0),
    SD_BUS_SIGNAL("StatusNotifierItemUnregistered", "s", 0),
    SD_BUS_SIGNAL("StatusNotifierHostRegistered", "", 0),
    SD_BUS_VTABLE_END};
