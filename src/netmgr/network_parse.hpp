#pragma once

// Pure text parsing and number formatting for the network code, kept apart so it can be
// tested without a network.

#include <map>
#include <string>
#include <vector>

#include "network_types.hpp"

namespace fleetwm::net {

// Signal level in dBm (-30 strong .. -100 unusable) as 0..100.
int dbm_to_percent(int dbm);

// /proc/net/wireless: interface -> link quality as 0..100.
std::map<std::string, int> parse_proc_wireless(const std::string& text);

// /proc/net/route: the default gateway of `iface` ("192.168.0.1"), or "".
std::string parse_default_gateway(const std::string& route_text, const std::string& iface);

// wpa_supplicant SCAN_RESULTS: networks in range, one entry per name (strongest kept),
// strongest first, hidden (empty-name) networks left out.
std::vector<AccessPoint> parse_wpa_scan_results(const std::string& text);

// wpa_supplicant STATUS / any "key=value" lines.
std::map<std::string, std::string> parse_key_values(const std::string& text);

struct WpaNetwork {
  int id = -1;
  std::string ssid;
  bool current = false;
};
// wpa_supplicant LIST_NETWORKS.
std::vector<WpaNetwork> parse_wpa_list_networks(const std::string& text);

// wpa_supplicant prints names with \xNN escapes for odd bytes; turns them back into bytes.
std::string unescape_wpa_ssid(const std::string& s);
// The name as lower-case hex, the form SET_NETWORK ssid accepts for any bytes.
std::string hex_encode(const std::string& s);

// "1 Gbit/s", "100 Mbit/s", "" for unknown.
std::string format_speed(int mbps);
// One line about a device for tooltips and the settings page, e.g. "Connected to Home (82%)".
std::string describe_device(const Device& d);
const char* state_label(State s);

// Which device the bar icon should represent: the connected one (Ethernet before Wi-Fi when
// both are up), else the first Wi-Fi card, else the first device; nullptr when none.
const Device* primary_device(const std::vector<Device>& devices);

}  // namespace fleetwm::net
