/* sd-bus vtables for the lock applet: the tray item (org.kde.StatusNotifierItem) and the
 * screensaver inhibit service (org.freedesktop.ScreenSaver). In C because the SD_BUS_*
 * macros use designated initializers that C++ rejects; handlers live in main.cpp. */
#include <systemd/sd-bus.h>

extern int lock_sni_get(sd_bus*, const char*, const char*, const char*, sd_bus_message*, void*, sd_bus_error*);
extern int lock_sni_activate(sd_bus_message*, void*, sd_bus_error*);
extern int lock_sni_ignore(sd_bus_message*, void*, sd_bus_error*);
extern int lock_ss_inhibit(sd_bus_message*, void*, sd_bus_error*);
extern int lock_ss_uninhibit(sd_bus_message*, void*, sd_bus_error*);
extern int lock_ss_get_active(sd_bus_message*, void*, sd_bus_error*);

const sd_bus_vtable lock_sni_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Category", "s", lock_sni_get, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Id", "s", lock_sni_get, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Title", "s", lock_sni_get, 0, 0),
    SD_BUS_PROPERTY("Status", "s", lock_sni_get, 0, 0),
    SD_BUS_PROPERTY("WindowId", "u", lock_sni_get, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("IconName", "s", lock_sni_get, 0, 0),
    SD_BUS_PROPERTY("IconPixmap", "a(iiay)", lock_sni_get, 0, 0),
    SD_BUS_PROPERTY("ToolTip", "(sa(iiay)ss)", lock_sni_get, 0, SD_BUS_VTABLE_PROPERTY_EMITS_INVALIDATION),
    SD_BUS_PROPERTY("ItemIsMenu", "b", lock_sni_get, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_METHOD("Activate", "ii", "", lock_sni_activate, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SecondaryActivate", "ii", "", lock_sni_activate, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ContextMenu", "ii", "", lock_sni_activate, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Scroll", "is", "", lock_sni_ignore, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("NewIcon", "", 0),
    SD_BUS_SIGNAL("NewTitle", "", 0),
    SD_BUS_SIGNAL("NewToolTip", "", 0),
    SD_BUS_VTABLE_END};

const sd_bus_vtable lock_screensaver_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Inhibit", "ss", "u", lock_ss_inhibit, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("UnInhibit", "u", "", lock_ss_uninhibit, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetActive", "", "b", lock_ss_get_active, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END};
