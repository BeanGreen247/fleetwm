#include "nm_backend.hpp"

#include <systemd/sd-bus.h>

#include <algorithm>
#include <cstring>

#include "network_parse.hpp"
#include "system_devices.hpp"

namespace fleetwm::net {

namespace {

constexpr const char* kNm = "org.freedesktop.NetworkManager";
constexpr const char* kNmPath = "/org/freedesktop/NetworkManager";
constexpr const char* kDeviceIface = "org.freedesktop.NetworkManager.Device";
constexpr const char* kWiredIface = "org.freedesktop.NetworkManager.Device.Wired";
constexpr const char* kWirelessIface = "org.freedesktop.NetworkManager.Device.Wireless";
constexpr const char* kApIface = "org.freedesktop.NetworkManager.AccessPoint";
constexpr const char* kSettingsPath = "/org/freedesktop/NetworkManager/Settings";
constexpr const char* kSettingsIface = "org.freedesktop.NetworkManager.Settings";
constexpr const char* kConnIface = "org.freedesktop.NetworkManager.Settings.Connection";

// NM device types and states (NMDeviceType, NMDeviceState).
constexpr unsigned kTypeEthernet = 1, kTypeWifi = 2, kTypeModem = 8;
constexpr unsigned kApFlagPrivacy = 1, kKeySae = 0x400;

struct Bus {
  sd_bus* bus = nullptr;
  Bus() {
    if (sd_bus_open_system(&bus) < 0) bus = nullptr;
    else sd_bus_set_method_call_timeout(bus, 5 * 1000000ULL);
  }
  ~Bus() {
    if (bus) sd_bus_flush_close_unref(bus);
  }
};

class Err {
 public:
  ~Err() { sd_bus_error_free(&e); }
  sd_bus_error e = SD_BUS_ERROR_NULL;
  std::string text() const { return e.message ? e.message : (e.name ? e.name : "unknown error"); }
};

class NmBackend : public Backend {
 public:
  Bus b;

  std::string str_prop(const char* path, const char* iface, const char* name) {
    Err err;
    char* v = nullptr;
    if (sd_bus_get_property_string(b.bus, kNm, path, iface, name, &err.e, &v) < 0) return "";
    std::string out = v ? v : "";
    free(v);
    return out;
  }
  unsigned u_prop(const char* path, const char* iface, const char* name, char type = 'u') {
    Err err;
    unsigned v = 0;
    uint8_t byte = 0;
    if (type == 'y') {
      if (sd_bus_get_property_trivial(b.bus, kNm, path, iface, name, &err.e, 'y', &byte) < 0) return 0;
      return byte;
    }
    if (sd_bus_get_property_trivial(b.bus, kNm, path, iface, name, &err.e, type, &v) < 0) return 0;
    return v;
  }
  // An object-path property; "" when unset ("/").
  std::string path_prop(const char* path, const char* iface, const char* name) {
    Err err;
    sd_bus_message* m = nullptr;
    if (sd_bus_get_property(b.bus, kNm, path, iface, name, &err.e, &m, "o") < 0) return "";
    const char* v = nullptr;
    std::string out;
    if (sd_bus_message_read(m, "o", &v) >= 0 && v) out = v;
    sd_bus_message_unref(m);
    return out == "/" ? "" : out;
  }
  std::vector<std::string> path_list(const std::string& path, const char* iface, const char* name) {
    std::vector<std::string> out;
    Err err;
    sd_bus_message* m = nullptr;
    if (sd_bus_get_property(b.bus, kNm, path.c_str(), iface, name, &err.e, &m, "ao") < 0) return out;
    if (sd_bus_message_enter_container(m, 'a', "o") >= 0) {
      const char* p;
      while (sd_bus_message_read(m, "o", &p) > 0) out.push_back(p);
      sd_bus_message_exit_container(m);
    }
    sd_bus_message_unref(m);
    return out;
  }
  std::string bytes_prop(const std::string& path, const char* iface, const char* name) {
    std::string out;
    Err err;
    sd_bus_message* m = nullptr;
    if (sd_bus_get_property(b.bus, kNm, path.c_str(), iface, name, &err.e, &m, "ay") < 0) return out;
    const void* data = nullptr;
    size_t len = 0;
    if (sd_bus_message_read_array(m, 'y', &data, &len) >= 0 && data) out.assign(static_cast<const char*>(data), len);
    sd_bus_message_unref(m);
    return out;
  }

  // Addresses ("192.168.0.5/24") and gateway from an IP4Config / IP6Config object.
  void read_ip(const std::string& cfg_path, const char* iface, Device* d) {
    if (cfg_path.empty()) return;
    Err err;
    sd_bus_message* m = nullptr;
    if (sd_bus_get_property(b.bus, kNm, cfg_path.c_str(), iface, "AddressData", &err.e, &m, "aa{sv}") >= 0) {
      if (sd_bus_message_enter_container(m, 'a', "a{sv}") >= 0) {
        while (sd_bus_message_enter_container(m, 'a', "{sv}") > 0) {
          std::string addr;
          unsigned prefix = 0;
          while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
            const char* key = nullptr;
            sd_bus_message_read(m, "s", &key);
            if (key && !std::strcmp(key, "address") && sd_bus_message_enter_container(m, 'v', "s") >= 0) {
              const char* a = nullptr;
              if (sd_bus_message_read(m, "s", &a) >= 0 && a) addr = a;
              sd_bus_message_exit_container(m);
            } else if (key && !std::strcmp(key, "prefix") && sd_bus_message_enter_container(m, 'v', "u") >= 0) {
              sd_bus_message_read(m, "u", &prefix);
              sd_bus_message_exit_container(m);
            } else {
              sd_bus_message_skip(m, "v");
            }
            sd_bus_message_exit_container(m);
          }
          sd_bus_message_exit_container(m);
          if (!addr.empty() && addr.rfind("fe80:", 0) != 0) d->addresses.push_back(addr + "/" + std::to_string(prefix));
        }
        sd_bus_message_exit_container(m);
      }
      sd_bus_message_unref(m);
    }
    if (d->gateway.empty()) d->gateway = str_prop(cfg_path.c_str(), iface, "Gateway");
  }

  // The SSIDs that have a saved profile.
  std::vector<std::pair<std::string, std::string>> saved_wifi() {  // (ssid, connection path)
    std::vector<std::pair<std::string, std::string>> out;
    for (const std::string& conn : path_list_settings()) {
      Err err;
      sd_bus_message* m = nullptr;
      if (sd_bus_call_method(b.bus, kNm, conn.c_str(), kConnIface, "GetSettings", &err.e, &m, "") < 0) continue;
      std::string ssid;
      if (sd_bus_message_enter_container(m, 'a', "{sa{sv}}") >= 0) {
        while (sd_bus_message_enter_container(m, 'e', "sa{sv}") > 0) {
          const char* section = nullptr;
          sd_bus_message_read(m, "s", &section);
          const bool wireless = section && !std::strcmp(section, "802-11-wireless");
          if (sd_bus_message_enter_container(m, 'a', "{sv}") >= 0) {
            while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
              const char* key = nullptr;
              sd_bus_message_read(m, "s", &key);
              if (wireless && key && !std::strcmp(key, "ssid") && sd_bus_message_enter_container(m, 'v', "ay") >= 0) {
                const void* data = nullptr;
                size_t len = 0;
                if (sd_bus_message_read_array(m, 'y', &data, &len) >= 0 && data) ssid.assign(static_cast<const char*>(data), len);
                sd_bus_message_exit_container(m);
              } else {
                sd_bus_message_skip(m, "v");
              }
              sd_bus_message_exit_container(m);
            }
            sd_bus_message_exit_container(m);
          }
          sd_bus_message_exit_container(m);
        }
        sd_bus_message_exit_container(m);
      }
      sd_bus_message_unref(m);
      if (!ssid.empty()) out.emplace_back(ssid, conn);
    }
    return out;
  }
  std::vector<std::string> path_list_settings() {
    std::vector<std::string> out;
    Err err;
    sd_bus_message* m = nullptr;
    if (sd_bus_call_method(b.bus, kNm, kSettingsPath, kSettingsIface, "ListConnections", &err.e, &m, "") < 0) return out;
    if (sd_bus_message_enter_container(m, 'a', "o") >= 0) {
      const char* p;
      while (sd_bus_message_read(m, "o", &p) > 0) out.push_back(p);
      sd_bus_message_exit_container(m);
    }
    sd_bus_message_unref(m);
    return out;
  }

  static State map_state(unsigned s) {
    if (s >= 100 && s < 110) return State::Connected;      // activated, deactivating
    if (s >= 40 && s <= 90) return State::Connecting;      // prepare .. secondaries
    if (s == 30) return State::Disconnected;               // disconnected
    if (s == 120) return State::Disconnected;              // failed
    return State::Unavailable;                             // unmanaged, unavailable, unknown
  }

  Snapshot snapshot() override {
    Snapshot s;
    s.backend = "NetworkManager";
    s.can_control = true;
    s.can_switch_radio = true;
    if (!b.bus) return s;
    s.wifi_enabled = u_prop(kNmPath, kNm, "WirelessEnabled", 'b') != 0;
    const auto saved = saved_wifi();
    for (const std::string& dev : path_list(kNmPath, kNm, "AllDevices")) {
      const unsigned type = u_prop(dev.c_str(), kDeviceIface, "DeviceType");
      if (type != kTypeEthernet && type != kTypeWifi && type != kTypeModem) continue;
      Device d;
      d.kind = type == kTypeWifi ? Kind::Wifi : type == kTypeModem ? Kind::Mobile : Kind::Ethernet;
      d.name = str_prop(dev.c_str(), kDeviceIface, "Interface");
      d.driver = str_prop(dev.c_str(), kDeviceIface, "Driver");
      const unsigned st = u_prop(dev.c_str(), kDeviceIface, "State");
      d.state = map_state(st);
      if (st == 10) continue;  // unmanaged: handled by something else, not ours to show as ours
      const std::string active = path_prop(dev.c_str(), kDeviceIface, "ActiveConnection");
      if (!active.empty()) d.connection = str_prop(active.c_str(), "org.freedesktop.NetworkManager.Connection.Active", "Id");
      read_ip(path_prop(dev.c_str(), kDeviceIface, "Ip4Config"), "org.freedesktop.NetworkManager.IP4Config", &d);
      read_ip(path_prop(dev.c_str(), kDeviceIface, "Ip6Config"), "org.freedesktop.NetworkManager.IP6Config", &d);
      if (d.kind == Kind::Ethernet) {
        d.hw_addr = str_prop(dev.c_str(), kWiredIface, "HwAddress");
        d.speed_mbps = static_cast<int>(u_prop(dev.c_str(), kWiredIface, "Speed"));
        if (d.state == State::Unavailable && u_prop(dev.c_str(), kWiredIface, "Carrier", 'b') == 0) d.state = State::Unavailable;
      } else if (d.kind == Kind::Wifi) {
        d.hw_addr = str_prop(dev.c_str(), kWirelessIface, "HwAddress");
        const std::string active_ap = path_prop(dev.c_str(), kWirelessIface, "ActiveAccessPoint");
        for (const std::string& ap_path : path_list(dev, kWirelessIface, "AccessPoints")) {
          AccessPoint ap;
          ap.ssid = bytes_prop(ap_path, kApIface, "Ssid");
          if (ap.ssid.empty()) continue;
          ap.bssid = str_prop(ap_path.c_str(), kApIface, "HwAddress");
          ap.strength = static_cast<int>(u_prop(ap_path.c_str(), kApIface, "Strength", 'y'));
          ap.frequency_mhz = static_cast<int>(u_prop(ap_path.c_str(), kApIface, "Frequency"));
          const unsigned flags = u_prop(ap_path.c_str(), kApIface, "Flags");
          ap.secured = (flags & kApFlagPrivacy) || u_prop(ap_path.c_str(), kApIface, "WpaFlags") ||
                       u_prop(ap_path.c_str(), kApIface, "RsnFlags");
          ap.active = ap_path == active_ap;
          ap.saved = std::any_of(saved.begin(), saved.end(), [&](const auto& sv) { return sv.first == ap.ssid; });
          if (ap.active) {
            d.signal = ap.strength;
            d.connection = ap.ssid;
          }
          auto same = std::find_if(d.access_points.begin(), d.access_points.end(),
                                   [&](const AccessPoint& o) { return o.ssid == ap.ssid; });
          if (same == d.access_points.end()) d.access_points.push_back(ap);
          else if (ap.active || (!same->active && ap.strength > same->strength)) *same = ap;
        }
        std::stable_sort(d.access_points.begin(), d.access_points.end(),
                         [](const AccessPoint& a, const AccessPoint& c) { return a.strength > c.strength; });
      }
      // StateReason (uu): a failed or dropped attempt because of the secrets means a wrong password.
      if (d.state == State::Disconnected) {
        Err err;
        sd_bus_message* m = nullptr;
        if (sd_bus_get_property(b.bus, kNm, dev.c_str(), kDeviceIface, "StateReason", &err.e, &m, "(uu)") >= 0) {
          unsigned state = 0, reason = 0;
          if (sd_bus_message_read(m, "(uu)", &state, &reason) >= 0 && st == 120 && reason >= 8 && reason <= 12)
            d.failure = "Wrong password, or the network refused the connection";
          sd_bus_message_unref(m);
        }
      }
      s.devices.push_back(std::move(d));
    }
    // Cards NetworkManager leaves alone (managed by ifupdown, systemd-networkd, ...) still show, read-only.
    std::vector<Device> others = read_system_devices();
    fill_addresses(&others);
    for (Device& o : others) {
      if (std::any_of(s.devices.begin(), s.devices.end(), [&](const Device& d) { return d.name == o.name; })) continue;
      o.controllable = false;
      s.devices.push_back(std::move(o));
    }
    std::sort(s.devices.begin(), s.devices.end(), [](const Device& a, const Device& c) {
      if (a.kind != c.kind) return a.kind < c.kind;
      return a.name < c.name;
    });
    return s;
  }

  std::string device_path(const std::string& name) {
    Err err;
    sd_bus_message* m = nullptr;
    if (sd_bus_call_method(b.bus, kNm, kNmPath, kNm, "GetDeviceByIpIface", &err.e, &m, "s", name.c_str()) < 0) return "";
    const char* path = nullptr;
    std::string out;
    if (sd_bus_message_read(m, "o", &path) >= 0 && path) out = path;
    sd_bus_message_unref(m);
    return out;
  }

  void scan(const std::string& device) override {
    const std::string dev = device_path(device);
    if (dev.empty()) return;
    Err err;
    sd_bus_message* m = nullptr;
    if (sd_bus_message_new_method_call(b.bus, &m, kNm, dev.c_str(), kWirelessIface, "RequestScan") < 0) return;
    sd_bus_message_append(m, "a{sv}", 0);
    sd_bus_call(b.bus, m, 0, &err.e, nullptr);
    sd_bus_message_unref(m);
  }

  bool append_string_entry(sd_bus_message* m, const char* key, const char* value) {
    return sd_bus_message_open_container(m, 'e', "sv") >= 0 && sd_bus_message_append(m, "s", key) >= 0 &&
           sd_bus_message_open_container(m, 'v', "s") >= 0 && sd_bus_message_append(m, "s", value) >= 0 &&
           sd_bus_message_close_container(m) >= 0 && sd_bus_message_close_container(m) >= 0;
  }
  bool append_bool_entry(sd_bus_message* m, const char* key, bool value) {
    return sd_bus_message_open_container(m, 'e', "sv") >= 0 && sd_bus_message_append(m, "s", key) >= 0 &&
           sd_bus_message_open_container(m, 'v', "b") >= 0 && sd_bus_message_append(m, "b", value ? 1 : 0) >= 0 &&
           sd_bus_message_close_container(m) >= 0 && sd_bus_message_close_container(m) >= 0;
  }

  bool connect(const std::string& device, const std::string& ssid, const std::string& password, std::string* error) override {
    const std::string dev = device_path(device);
    if (dev.empty()) {
      if (error) *error = "NetworkManager does not know " + device;
      return false;
    }
    // The matching access point (for the specific object and the security type).
    std::string ap_path;
    unsigned rsn = 0;
    for (const std::string& p : path_list(dev, kWirelessIface, "AccessPoints")) {
      if (bytes_prop(p, kApIface, "Ssid") != ssid) continue;
      if (ap_path.empty() || u_prop(p.c_str(), kApIface, "Strength", 'y') > u_prop(ap_path.c_str(), kApIface, "Strength", 'y')) {
        ap_path = p;
        rsn = u_prop(p.c_str(), kApIface, "RsnFlags");
      }
    }
    if (ap_path.empty()) ap_path = "/";

    Err err;
    sd_bus_message* reply = nullptr;
    if (password.empty()) {
      for (const auto& [name, conn] : saved_wifi()) {
        if (name != ssid) continue;  // a saved profile: just use it
        if (sd_bus_call_method(b.bus, kNm, kNmPath, kNm, "ActivateConnection", &err.e, &reply, "ooo", conn.c_str(),
                               dev.c_str(), ap_path.c_str()) < 0) {
          if (error) *error = err.text();
          return false;
        }
        sd_bus_message_unref(reply);
        return true;
      }
    }

    sd_bus_message* m = nullptr;
    if (sd_bus_message_new_method_call(b.bus, &m, kNm, kNmPath, kNm, "AddAndActivateConnection") < 0) return false;
    bool ok = sd_bus_message_open_container(m, 'a', "{sa{sv}}") >= 0;
    // connection
    ok = ok && sd_bus_message_open_container(m, 'e', "sa{sv}") >= 0 && sd_bus_message_append(m, "s", "connection") >= 0 &&
         sd_bus_message_open_container(m, 'a', "{sv}") >= 0 && append_string_entry(m, "type", "802-11-wireless") &&
         append_string_entry(m, "id", ssid.c_str()) && append_bool_entry(m, "autoconnect", true) &&
         sd_bus_message_close_container(m) >= 0 &&
         sd_bus_message_close_container(m) >= 0;
    // 802-11-wireless: the name as raw bytes
    ok = ok && sd_bus_message_open_container(m, 'e', "sa{sv}") >= 0 && sd_bus_message_append(m, "s", "802-11-wireless") >= 0 &&
         sd_bus_message_open_container(m, 'a', "{sv}") >= 0 && sd_bus_message_open_container(m, 'e', "sv") >= 0 &&
         sd_bus_message_append(m, "s", "ssid") >= 0 && sd_bus_message_open_container(m, 'v', "ay") >= 0 &&
         sd_bus_message_append_array(m, 'y', ssid.data(), ssid.size()) >= 0 && sd_bus_message_close_container(m) >= 0 &&
         sd_bus_message_close_container(m) >= 0 && sd_bus_message_close_container(m) >= 0 &&
         sd_bus_message_close_container(m) >= 0;
    if (!password.empty()) {
      const bool sae_only = (rsn & kKeySae) && !(rsn & 0x100);  // WPA3 networks without WPA2 fallback
      ok = ok && sd_bus_message_open_container(m, 'e', "sa{sv}") >= 0 &&
           sd_bus_message_append(m, "s", "802-11-wireless-security") >= 0 &&
           sd_bus_message_open_container(m, 'a', "{sv}") >= 0 &&
           append_string_entry(m, "key-mgmt", sae_only ? "sae" : "wpa-psk") &&
           append_string_entry(m, "psk", password.c_str()) && sd_bus_message_close_container(m) >= 0 &&
           sd_bus_message_close_container(m) >= 0;
    }
    ok = ok && sd_bus_message_close_container(m) >= 0 && sd_bus_message_append(m, "oo", dev.c_str(), ap_path.c_str()) >= 0;
    if (!ok) {
      sd_bus_message_unref(m);
      if (error) *error = "could not build the connection request";
      return false;
    }
    const int r = sd_bus_call(b.bus, m, 0, &err.e, &reply);
    sd_bus_message_unref(m);
    if (r < 0) {
      if (error) *error = err.text();
      return false;
    }
    sd_bus_message_unref(reply);
    return true;
  }

  bool disconnect(const std::string& device, std::string* error) override {
    const std::string dev = device_path(device);
    Err err;
    if (dev.empty() || sd_bus_call_method(b.bus, kNm, dev.c_str(), kDeviceIface, "Disconnect", &err.e, nullptr, "") < 0) {
      if (error) *error = dev.empty() ? "NetworkManager does not know " + device : err.text();
      return false;
    }
    return true;
  }

  bool forget(const std::string&, const std::string& ssid, std::string* error) override {
    bool removed = false;
    for (const auto& [name, conn] : saved_wifi()) {
      if (name != ssid) continue;
      Err err;
      if (sd_bus_call_method(b.bus, kNm, conn.c_str(), kConnIface, "Delete", &err.e, nullptr, "") >= 0) removed = true;
      else if (error) *error = err.text();
    }
    if (!removed && error && error->empty()) *error = "no saved network with that name";
    return removed;
  }

  bool set_wifi_enabled(bool on, std::string* error) override {
    Err err;
    if (sd_bus_set_property(b.bus, kNm, kNmPath, kNm, "WirelessEnabled", &err.e, "b", on ? 1 : 0) < 0) {
      if (error) *error = err.text();
      return false;
    }
    return true;
  }
};

}  // namespace

std::unique_ptr<Backend> make_nm_backend() {
  auto nm = std::make_unique<NmBackend>();
  if (!nm->b.bus) return nullptr;
  // Running means: someone owns the NetworkManager name on the system bus.
  Err err;
  sd_bus_message* m = nullptr;
  int has_owner = 0;
  if (sd_bus_call_method(nm->b.bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                         "NameHasOwner", &err.e, &m, "s", kNm) < 0)
    return nullptr;
  sd_bus_message_read(m, "b", &has_owner);
  sd_bus_message_unref(m);
  if (!has_owner) return nullptr;
  return nm;
}

}  // namespace fleetwm::net
