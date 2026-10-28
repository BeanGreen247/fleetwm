/* sd-bus vtable for org.kde.KWin.InputDevice, the interface KDE's tools (TuxThrottle's touchpad switch, kcm scripts, `busctl --user tree org.kde.KWin`) use to read and
 * change a pointer device. Lives in a C file because the SD_BUS_* macros use C99 designated initializers that C++ rejects; the handlers are in kwin_input.cpp. */
#include <systemd/sd-bus.h>

extern int fleetwm_kwin_prop_get(sd_bus*, const char*, const char*, const char*, sd_bus_message*, void*, sd_bus_error*);
extern int fleetwm_kwin_prop_set(sd_bus*, const char*, const char*, const char*, sd_bus_message*, void*, sd_bus_error*);
extern int fleetwm_kwin_accel_get(sd_bus*, const char*, const char*, const char*, sd_bus_message*, void*, sd_bus_error*);
extern int fleetwm_kwin_accel_set(sd_bus*, const char*, const char*, const char*, sd_bus_message*, void*, sd_bus_error*);
extern int fleetwm_kwin_names_get(sd_bus*, const char*, const char*, const char*, sd_bus_message*, void*, sd_bus_error*);

const sd_bus_vtable fleetwm_kwin_input_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("name", "s", fleetwm_kwin_prop_get, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("sysName", "s", fleetwm_kwin_prop_get, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("touchpad", "b", fleetwm_kwin_prop_get, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("pointer", "b", fleetwm_kwin_prop_get, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("keyboard", "b", fleetwm_kwin_prop_get, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("supportsDisableEvents", "b", fleetwm_kwin_prop_get, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_WRITABLE_PROPERTY("enabled", "b", fleetwm_kwin_prop_get, fleetwm_kwin_prop_set, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_WRITABLE_PROPERTY("tapToClick", "b", fleetwm_kwin_prop_get, fleetwm_kwin_prop_set, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_WRITABLE_PROPERTY("naturalScroll", "b", fleetwm_kwin_prop_get, fleetwm_kwin_prop_set, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_WRITABLE_PROPERTY("disableWhileTyping", "b", fleetwm_kwin_prop_get, fleetwm_kwin_prop_set, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_WRITABLE_PROPERTY("leftHanded", "b", fleetwm_kwin_prop_get, fleetwm_kwin_prop_set, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    /* the pointer acceleration the osu!lazer launcher and KDE's mouse settings set: flat (raw, what games want) or adaptive, and the speed from -1 to 1 */
    SD_BUS_WRITABLE_PROPERTY("pointerAccelerationProfileFlat", "b", fleetwm_kwin_prop_get, fleetwm_kwin_prop_set, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_WRITABLE_PROPERTY("pointerAccelerationProfileAdaptive", "b", fleetwm_kwin_prop_get, fleetwm_kwin_prop_set, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_WRITABLE_PROPERTY("pointerAcceleration", "d", fleetwm_kwin_accel_get, fleetwm_kwin_accel_set, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_VTABLE_END};

/* org.kde.KWin.InputDeviceManager at /org/kde/KWin/InputDevice: the list of device names the scripts walk before reading a device */
const sd_bus_vtable fleetwm_kwin_manager_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("devicesSysNames", "as", fleetwm_kwin_names_get, 0, 0),
    SD_BUS_VTABLE_END};
