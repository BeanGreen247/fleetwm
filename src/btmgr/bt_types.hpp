#pragma once

// Bluetooth state as the bar and Settings see it. Plain data plus a few pure helpers (tested); the BlueZ side is in bt_backend.

#include <string>
#include <vector>

namespace fleetwm::bt {

enum class DeviceKind { Other, Audio, Headset, Input, Phone, Computer, Gamepad, Camera, Printer };

struct Device {
  std::string path;     // D-Bus object path, e.g. /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF
  std::string address;  // AA:BB:CC:DD:EE:FF
  std::string name;     // alias the user sees ("" when the device never gave one)
  std::string icon;     // BlueZ icon name: audio-headset, input-keyboard, phone, ...
  bool paired = false;
  bool connected = false;
  bool trusted = false;
  bool connecting = false;  // a connect or pair call is in flight (set by the UI, not by BlueZ)
  int rssi = 0;             // dBm while scanning, 0 when unknown
  int battery = -1;         // percent, -1 when the device does not report it
};

struct Adapter {
  std::string path;   // /org/bluez/hci0
  std::string name;   // alias of this computer
  std::string address;
  bool powered = false;
  bool discovering = false;
  bool discoverable = false;
};

struct State {
  bool available = false;  // BlueZ is running and an adapter exists
  Adapter adapter;
  std::vector<Device> devices;
  std::string error;  // last failed action, "" when fine
};

// What to show for the bar icon.
enum class Glyph { Off, On, Connected };

DeviceKind kind_from_icon(const std::string& icon);
const char* kind_label(DeviceKind kind);  // "Headphones", "Keyboard", ...

// The name to show: the alias, else the address.
std::string display_name(const Device& d);

// True for a nearby device that never told its name (BlueZ then uses the address, with dashes, as the alias).
bool is_unnamed(const Device& d);

// Paired and connected devices first, then paired, then the nearby ones by signal strength (strongest first), then by name.
void sort_devices(std::vector<Device>* devices);

Glyph glyph_for(const State& s);

// One line for a device row: "Connected", "Paired", "Pairing...", "Nearby", with the battery when known.
std::string status_line(const Device& d);

// The tooltip of the bar icon: "Bluetooth: off", "Bluetooth: on", "Bluetooth: connected to Name, Name2".
std::string tooltip_text(const State& s);

// "AA_BB_CC_DD_EE_FF" from an object path suffix -> "AA:BB:CC:DD:EE:FF"; "" when it is not an address.
std::string address_from_path(const std::string& path);

// A short sentence for a BlueZ / D-Bus error name ("org.bluez.Error.AuthenticationFailed"); `message` is used when the name is not known.
std::string friendly_error(const std::string& name, const std::string& message);

}  // namespace fleetwm::bt
