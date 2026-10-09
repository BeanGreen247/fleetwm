// Bluetooth helpers: device kinds, ordering, the bar icon state, texts, error sentences. (The BlueZ calls need a real adapter.)
#include <gtest/gtest.h>

#include "bt_types.hpp"

using namespace fleetwm::bt;

namespace {
Device dev(const char* name, bool paired, bool connected, int rssi = 0) {
  Device d;
  d.name = name;
  d.address = "AA:BB:CC:DD:EE:FF";
  d.paired = paired;
  d.connected = connected;
  d.rssi = rssi;
  return d;
}
State on_state() {
  State s;
  s.available = true;
  s.adapter.powered = true;
  return s;
}
}  // namespace

TEST(BtTypes, KindFromIconName) {
  EXPECT_EQ(kind_from_icon("audio-headset"), DeviceKind::Headset);
  EXPECT_EQ(kind_from_icon("audio-headphones"), DeviceKind::Headset);
  EXPECT_EQ(kind_from_icon("audio-card"), DeviceKind::Audio);
  EXPECT_EQ(kind_from_icon("input-keyboard"), DeviceKind::Input);
  EXPECT_EQ(kind_from_icon("input-mouse"), DeviceKind::Input);
  EXPECT_EQ(kind_from_icon("input-gaming"), DeviceKind::Gamepad);
  EXPECT_EQ(kind_from_icon("phone"), DeviceKind::Phone);
  EXPECT_EQ(kind_from_icon("computer"), DeviceKind::Computer);
  EXPECT_EQ(kind_from_icon(""), DeviceKind::Other);
  EXPECT_STREQ(kind_label(DeviceKind::Headset), "Headphones");
  EXPECT_STREQ(kind_label(DeviceKind::Other), "Device");
}

TEST(BtTypes, DisplayNameFallsBackToAddress) {
  Device d;
  d.address = "11:22:33:44:55:66";
  EXPECT_EQ(display_name(d), "11:22:33:44:55:66");
  d.name = "Buds";
  EXPECT_EQ(display_name(d), "Buds");
}

TEST(BtTypes, ConnectedFirstThenPairedThenNearbyByStrength) {
  std::vector<Device> v = {dev("Far", false, false, -85), dev("Paired B", true, false), dev("Near", false, false, -40),
                           dev("Linked", true, true), dev("Paired A", true, false), dev("Unknown", false, false, 0)};
  sort_devices(&v);
  ASSERT_EQ(v.size(), 6u);
  EXPECT_EQ(v[0].name, "Linked");
  EXPECT_EQ(v[1].name, "Paired A");
  EXPECT_EQ(v[2].name, "Paired B");
  EXPECT_EQ(v[3].name, "Near");
  EXPECT_EQ(v[4].name, "Far");
  EXPECT_EQ(v[5].name, "Unknown");  // no reading counts as the weakest
}

TEST(BtTypes, GlyphFollowsPowerAndConnections) {
  State s;
  EXPECT_EQ(glyph_for(s), Glyph::Off);  // no adapter
  s = on_state();
  s.adapter.powered = false;
  EXPECT_EQ(glyph_for(s), Glyph::Off);
  s.adapter.powered = true;
  s.devices = {dev("A", true, false)};
  EXPECT_EQ(glyph_for(s), Glyph::On);
  s.devices.push_back(dev("B", true, true));
  EXPECT_EQ(glyph_for(s), Glyph::Connected);
}

TEST(BtTypes, TooltipNamesTheConnectedDevices) {
  State s;
  EXPECT_EQ(tooltip_text(s), "Bluetooth: no adapter");
  s = on_state();
  s.adapter.powered = false;
  EXPECT_EQ(tooltip_text(s), "Bluetooth: off");
  s.adapter.powered = true;
  EXPECT_EQ(tooltip_text(s), "Bluetooth: on");
  s.devices = {dev("Buds", true, true), dev("Mouse", true, true), dev("Phone", true, false)};
  EXPECT_EQ(tooltip_text(s), "Bluetooth: connected to Buds, Mouse");
}

TEST(BtTypes, StatusLine) {
  Device d = dev("X", false, false);
  EXPECT_EQ(status_line(d), "Nearby");
  d.paired = true;
  EXPECT_EQ(status_line(d), "Paired");
  d.connected = true;
  EXPECT_EQ(status_line(d), "Connected");
  d.battery = 80;
  EXPECT_EQ(status_line(d), "Connected, battery 80%");
  d.connecting = true;
  d.connected = false;
  EXPECT_EQ(status_line(d), "Connecting...");
}

TEST(BtTypes, AddressFromObjectPath) {
  EXPECT_EQ(address_from_path("/org/bluez/hci0/dev_E3_4B_AE_A7_EF_B3"), "E3:4B:AE:A7:EF:B3");
  EXPECT_EQ(address_from_path("/org/bluez/hci0/dev_E3_4B_AE_A7_EF_B3/service0001/char0002"), "E3:4B:AE:A7:EF:B3");
  EXPECT_EQ(address_from_path("/org/bluez/hci0"), "");
  EXPECT_EQ(address_from_path("/org/bluez/hci0/dev_E3_4B_AE"), "");
  EXPECT_EQ(address_from_path("/org/bluez/hci0/dev_ZZ_4B_AE_A7_EF_B3"), "");
}

TEST(BtTypes, FriendlyErrors) {
  EXPECT_EQ(friendly_error("org.bluez.Error.AuthenticationFailed", "x"), "Pairing failed: the PIN or passkey was wrong.");
  EXPECT_EQ(friendly_error("org.bluez.Error.ConnectionAttemptFailed", ""), "Could not connect. Is the device on and in range?");
  EXPECT_EQ(friendly_error("org.freedesktop.DBus.Error.NoReply", ""), "The device did not answer in time.");
  EXPECT_EQ(friendly_error("org.freedesktop.DBus.Error.ServiceUnknown", ""), "Bluetooth is not running.");
  EXPECT_EQ(friendly_error("org.example.Weird", "something odd"), "something odd");
  EXPECT_EQ(friendly_error("org.example.Weird", ""), "org.example.Weird");
  EXPECT_EQ(friendly_error("", ""), "The action failed.");
}

TEST(BtTypes, UnnamedNearbyDevices) {
  Device d;
  d.address = "AA:BB:CC:DD:EE:FF";
  d.name = "AA-BB-CC-DD-EE-FF";  // what BlueZ shows for a device that never gave a name
  EXPECT_TRUE(is_unnamed(d));
  d.name = "";
  EXPECT_TRUE(is_unnamed(d));
  d.name = "Buds";
  EXPECT_FALSE(is_unnamed(d));
  d.name = "AA-BB-CC-DD-EE-FF";
  d.paired = true;
  EXPECT_FALSE(is_unnamed(d)) << "a paired device always shows";
}
