#!/usr/bin/env python3
# A fake libappindicator-style tray item (what Steam registers): a StatusNotifierItem WITHOUT Activate / ContextMenu, with a Menu property that
# points at a com.canonical.dbusmenu object. Prints "EVENT <id>" when a menu entry is chosen. The icon is a solid magenta square.
import dbus, dbus.service, dbus.mainloop.glib, sys
from gi.repository import GLib
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
name = dbus.service.BusName("org.test.SniMenu", bus)
ITEM = "org.kde.StatusNotifierItem"
px = bytes([0xff, 0xff, 0x00, 0xff]) * (22 * 22)  # ARGB, network order: A R G B
props = {
    "Id": "snitest", "Title": "Test", "Category": "ApplicationStatus", "Status": "Active", "IconName": "",
    "Menu": dbus.ObjectPath("/Menu"),
    "IconPixmap": dbus.Array([dbus.Struct((22, 22, dbus.Array(px, signature="y")), signature="iiay")], signature="(iiay)"),
}
class Item(dbus.service.Object):
    @dbus.service.method("org.freedesktop.DBus.Properties", in_signature="ss", out_signature="v")
    def Get(self, i, p): return props[p]
    @dbus.service.method("org.freedesktop.DBus.Properties", in_signature="s", out_signature="a{sv}")
    def GetAll(self, i): return props
    @dbus.service.method(ITEM, in_signature="ii")
    def SecondaryActivate(self, x, y): pass
class Menu(dbus.service.Object):
    @dbus.service.method("com.canonical.dbusmenu", in_signature="i", out_signature="b")
    def AboutToShow(self, i): return False
    @dbus.service.method("com.canonical.dbusmenu", in_signature="iias", out_signature="u(ia{sv}av)")
    def GetLayout(self, parent, depth, names):
        def leaf(i, label, **kw):
            d = {"label": label}; d.update(kw)
            return dbus.Struct((i, dbus.Dictionary(d, signature="sv"), dbus.Array([], signature="v")), signature="ia{sv}av")
        sub = dbus.Struct((9, dbus.Dictionary({"label": "_Friends", "children-display": "submenu"}, signature="sv"),
                           dbus.Array([leaf(10, "Online"), leaf(11, "Away")], signature="v")), signature="ia{sv}av")
        kids = [leaf(1, "_Store"), leaf(2, "_Library"), leaf(3, "", type="separator"), sub, leaf(4, "Hidden", visible=False), leaf(5, "_Exit")]
        root = dbus.Struct((0, dbus.Dictionary({"children-display": "submenu"}, signature="sv"), dbus.Array(kids, signature="v")), signature="ia{sv}av")
        return dbus.UInt32(1), root
    @dbus.service.method("com.canonical.dbusmenu", in_signature="isvu")
    def Event(self, i, ev, data, ts):
        print("EVENT", int(i), str(ev), flush=True)
Item(bus, "/StatusNotifierItem"); Menu(bus, "/Menu")
def register():
    try:
        bus.get_object("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher").RegisterStatusNotifierItem("org.test.SniMenu", dbus_interface="org.kde.StatusNotifierWatcher")
        print("REGISTERED", flush=True)
    except Exception as e:
        print("WAIT", e, flush=True); return True
    return False
GLib.timeout_add(500, register)
GLib.MainLoop().run()
