#include "bt_types.hpp"

#include <algorithm>
#include <cctype>

namespace fleetwm::bt {

DeviceKind kind_from_icon(const std::string& icon) {
  auto has = [&](const char* s) { return icon.find(s) != std::string::npos; };
  if (has("headset") || has("headphones")) return DeviceKind::Headset;
  if (has("audio") || has("speaker")) return DeviceKind::Audio;
  if (has("input-gaming")) return DeviceKind::Gamepad;
  if (has("input-")) return DeviceKind::Input;
  if (has("phone")) return DeviceKind::Phone;
  if (has("computer") || has("laptop")) return DeviceKind::Computer;
  if (has("camera") || has("video")) return DeviceKind::Camera;
  if (has("printer")) return DeviceKind::Printer;
  return DeviceKind::Other;
}

const char* kind_label(DeviceKind kind) {
  switch (kind) {
    case DeviceKind::Audio: return "Speaker";
    case DeviceKind::Headset: return "Headphones";
    case DeviceKind::Input: return "Input device";
    case DeviceKind::Phone: return "Phone";
    case DeviceKind::Computer: return "Computer";
    case DeviceKind::Gamepad: return "Game controller";
    case DeviceKind::Camera: return "Camera";
    case DeviceKind::Printer: return "Printer";
    case DeviceKind::Other: break;
  }
  return "Device";
}

bool is_unnamed(const Device& d) {
  if (d.paired || d.connected) return false;
  if (d.name.empty()) return true;
  std::string dashed = d.address;
  for (char& c : dashed)
    if (c == ':') c = '-';
  return d.name == dashed || d.name == d.address;
}

std::string display_name(const Device& d) { return d.name.empty() ? d.address : d.name; }

void sort_devices(std::vector<Device>* devices) {
  auto rank = [](const Device& d) { return d.connected ? 0 : d.paired ? 1 : 2; };
  std::stable_sort(devices->begin(), devices->end(), [&](const Device& a, const Device& b) {
    if (rank(a) != rank(b)) return rank(a) < rank(b);
    // dBm: -40 is closer than -80. 0 means unknown, which counts as the weakest.
    const int ra = a.rssi == 0 ? -127 : a.rssi, rb = b.rssi == 0 ? -127 : b.rssi;
    if (rank(a) == 2 && ra != rb) return ra > rb;
    return display_name(a) < display_name(b);
  });
}

Glyph glyph_for(const State& s) {
  if (!s.available || !s.adapter.powered) return Glyph::Off;
  for (const Device& d : s.devices)
    if (d.connected) return Glyph::Connected;
  return Glyph::On;
}

std::string status_line(const Device& d) {
  std::string out = d.connecting ? "Connecting..." : d.connected ? "Connected" : d.paired ? "Paired" : "Nearby";
  if (d.connected && d.battery >= 0) out += ", battery " + std::to_string(d.battery) + "%";
  return out;
}

std::string tooltip_text(const State& s) {
  if (!s.available) return "Bluetooth: no adapter";
  if (!s.adapter.powered) return "Bluetooth: off";
  std::string names;
  for (const Device& d : s.devices)
    if (d.connected) names += (names.empty() ? "" : ", ") + display_name(d);
  return names.empty() ? "Bluetooth: on" : "Bluetooth: connected to " + names;
}

std::string address_from_path(const std::string& path) {
  const size_t at = path.rfind("/dev_");
  if (at == std::string::npos) return "";
  std::string tail = path.substr(at + 5);
  const size_t slash = tail.find('/');
  if (slash != std::string::npos) tail.resize(slash);
  if (tail.size() != 17) return "";
  for (size_t i = 0; i < tail.size(); ++i) {
    if (i % 3 == 2) {
      if (tail[i] != '_') return "";
      tail[i] = ':';
    } else if (!std::isxdigit(static_cast<unsigned char>(tail[i]))) {
      return "";
    }
  }
  return tail;
}

std::string friendly_error(const std::string& name, const std::string& message) {
  struct Known {
    const char* tail;
    const char* text;
  };
  static const Known known[] = {
      {"AuthenticationFailed", "Pairing failed: the PIN or passkey was wrong."},
      {"AuthenticationCanceled", "Pairing was cancelled."},
      {"AuthenticationRejected", "The device rejected the pairing."},
      {"AuthenticationTimeout", "The device did not answer in time."},
      {"ConnectionAttemptFailed", "Could not connect. Is the device on and in range?"},
      {"AlreadyExists", "Already paired."},
      {"AlreadyConnected", "Already connected."},
      {"NotReady", "The Bluetooth adapter is not ready."},
      {"NotAvailable", "That is not available right now."},
      {"InProgress", "Another Bluetooth action is still running."},
      {"Rejected", "Rejected."},
      {"Canceled", "Cancelled."},
      {"Failed", "The action failed."},
      {"NotPermitted", "Not permitted."},
      {"NotSupported", "The device does not support that."},
      {"DoesNotExist", "That device is no longer known."},
      {"InvalidArguments", "The device could not be addressed."},
  };
  const size_t dot = name.rfind('.');
  const std::string tail = dot == std::string::npos ? name : name.substr(dot + 1);
  if (name.rfind("org.bluez.Error.", 0) == 0)
    for (const Known& k : known)
      if (tail == k.tail) return k.text;
  if (name == "org.freedesktop.DBus.Error.NoReply" || name == "org.freedesktop.DBus.Error.Timeout")
    return "The device did not answer in time.";
  if (name == "org.freedesktop.DBus.Error.ServiceUnknown") return "Bluetooth is not running.";
  if (name == "org.freedesktop.DBus.Error.AccessDenied") return "Not allowed to change Bluetooth settings.";
  return message.empty() ? (name.empty() ? "The action failed." : name) : message;
}

}  // namespace fleetwm::bt
