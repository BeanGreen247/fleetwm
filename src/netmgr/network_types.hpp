#pragma once

// What the network tab and the bar's network icon work with, independent of which program
// actually manages the network (NetworkManager, wpa_supplicant, or nothing we can talk to).

#include <memory>
#include <string>
#include <vector>

namespace fleetwm::net {

enum class Kind { Ethernet, Wifi, Other };
enum class State {
  Unavailable,   // no cable, radio switched off, or not usable
  Disconnected,  // usable but not connected
  Connecting,
  Connected,
};

struct AccessPoint {
  std::string ssid;
  std::string bssid;
  int strength = 0;  // 0..100
  bool secured = false;
  bool active = false;  // the one this card is connected to
  bool saved = false;   // a saved profile exists, so no password is needed
  int frequency_mhz = 0;
};

struct Device {
  std::string name;  // "enp3s0", "wlan0"
  Kind kind = Kind::Other;
  State state = State::Unavailable;
  std::string hw_addr;
  std::string driver;
  std::vector<std::string> addresses;  // "192.168.0.5/24"
  std::string gateway;
  int speed_mbps = 0;       // Ethernet link speed, 0 = unknown
  std::string connection;   // Wi-Fi network name or profile name
  int signal = 0;           // Wi-Fi strength of the current connection, 0..100
  std::string failure;      // why the last connect attempt failed, when known
  bool controllable = true; // false: shown from the kernel's view only, something else manages it
  std::vector<AccessPoint> access_points;  // Wi-Fi: networks in range, strongest first
};

struct Snapshot {
  std::vector<Device> devices;
  bool wifi_enabled = true;  // radio switch (only meaningful when can_switch_radio)
  bool can_switch_radio = false;
  bool can_control = false;  // connecting/disconnecting is possible
  std::string backend;       // "NetworkManager", "wpa_supplicant", "System status only"
  std::string note;          // one line for the user when something limits what is shown
};

class Backend {
 public:
  virtual ~Backend() = default;
  virtual Snapshot snapshot() = 0;
  // Asks the Wi-Fi card to look for networks; results show up in later snapshots.
  virtual void scan(const std::string& device) = 0;
  // `password` is empty for open networks and for ones with a saved profile. Returns false
  // with a message in *error when the request could not even be made.
  virtual bool connect(const std::string& device, const std::string& ssid, const std::string& password,
                       std::string* error) = 0;
  virtual bool disconnect(const std::string& device, std::string* error) = 0;
  virtual bool forget(const std::string& device, const std::string& ssid, std::string* error) = 0;
  virtual bool set_wifi_enabled(bool, std::string* error) {
    if (error) *error = "this network manager has no radio switch";
    return false;
  }
};

// The best backend available right now: NetworkManager if it is running, else a running
// wpa_supplicant, else the read-only system status.
std::unique_ptr<Backend> make_backend();

}  // namespace fleetwm::net
