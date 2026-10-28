/* sd-bus vtable for org.freedesktop.Notifications. In a C file because the SD_BUS_* macros use C99 designated initializers that C++ rejects;
 * the handlers are in main.cpp (extern "C"). */
#include <systemd/sd-bus.h>

extern int fleetwm_notifyd_caps(sd_bus_message*, void*, sd_bus_error*);
extern int fleetwm_notifyd_notify(sd_bus_message*, void*, sd_bus_error*);
extern int fleetwm_notifyd_close(sd_bus_message*, void*, sd_bus_error*);
extern int fleetwm_notifyd_info(sd_bus_message*, void*, sd_bus_error*);

const sd_bus_vtable fleetwm_notifyd_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetCapabilities", "", "as", fleetwm_notifyd_caps, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Notify", "susssasa{sv}i", "u", fleetwm_notifyd_notify, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("CloseNotification", "u", "", fleetwm_notifyd_close, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetServerInformation", "", "ssss", fleetwm_notifyd_info, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("NotificationClosed", "uu", 0),
    SD_BUS_SIGNAL("ActionInvoked", "us", 0),
    SD_BUS_VTABLE_END};
