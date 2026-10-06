#include "network_parse.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace fleetwm::net {

int dbm_to_percent(int dbm) { return std::clamp((dbm + 100) * 2, 0, 100); }

std::map<std::string, int> parse_proc_wireless(const std::string& text) {
  std::map<std::string, int> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;  // the two header lines have no "iface:"
    std::string iface = line.substr(0, colon);
    iface.erase(0, iface.find_first_not_of(' '));
    std::istringstream fields(line.substr(colon + 1));
    std::string status, quality;
    if (!(fields >> status >> quality)) continue;
    const double q = std::atof(quality.c_str());  // "70." out of 70
    out[iface] = std::clamp(static_cast<int>(q * 100.0 / 70.0 + 0.5), 0, 100);
  }
  return out;
}

std::string parse_default_gateway(const std::string& route_text, const std::string& iface) {
  std::istringstream in(route_text);
  std::string line;
  std::getline(in, line);  // header
  while (std::getline(in, line)) {
    std::istringstream f(line);
    std::string name, dest, gw;
    if (!(f >> name >> dest >> gw) || name != iface || dest != "00000000") continue;
    unsigned long v = std::strtoul(gw.c_str(), nullptr, 16);  // little-endian hex
    if (v == 0) continue;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%lu.%lu.%lu.%lu", v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >> 24) & 0xff);
    return buf;
  }
  return "";
}

std::string unescape_wpa_ssid(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      const char c = s[i + 1];
      if (c == 'x' && i + 3 < s.size() + 0 && std::isxdigit(static_cast<unsigned char>(s[i + 2])) &&
          std::isxdigit(static_cast<unsigned char>(s[i + 3]))) {
        out += static_cast<char>(std::strtol(s.substr(i + 2, 2).c_str(), nullptr, 16));
        i += 3;
        continue;
      }
      if (c == '\\' || c == '"') {
        out += c;
        ++i;
        continue;
      }
      if (c == 'n') {
        out += '\n';
        ++i;
        continue;
      }
    }
    out += s[i];
  }
  return out;
}

std::string hex_encode(const std::string& s) {
  static const char* d = "0123456789abcdef";
  std::string out;
  for (unsigned char c : s) {
    out += d[c >> 4];
    out += d[c & 15];
  }
  return out;
}

std::vector<AccessPoint> parse_wpa_scan_results(const std::string& text) {
  std::vector<AccessPoint> aps;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    // bssid \t frequency \t signal level \t flags \t ssid
    std::vector<std::string> f;
    std::stringstream ls(line);
    std::string cell;
    while (std::getline(ls, cell, '\t')) f.push_back(cell);
    if (f.size() < 5 || f[0] == "bssid / frequency / signal level / flags / ssid") continue;
    AccessPoint ap;
    ap.bssid = f[0];
    ap.frequency_mhz = std::atoi(f[1].c_str());
    ap.strength = dbm_to_percent(std::atoi(f[2].c_str()));
    ap.secured = f[3].find("WPA") != std::string::npos || f[3].find("WEP") != std::string::npos ||
                 f[3].find("SAE") != std::string::npos || f[3].find("RSN") != std::string::npos;
    ap.ssid = unescape_wpa_ssid(f[4]);
    if (ap.ssid.empty() || ap.ssid.find('\0') != std::string::npos) continue;
    auto same = std::find_if(aps.begin(), aps.end(), [&](const AccessPoint& o) { return o.ssid == ap.ssid; });
    if (same == aps.end()) aps.push_back(ap);
    else if (ap.strength > same->strength) *same = ap;
  }
  std::stable_sort(aps.begin(), aps.end(), [](const AccessPoint& a, const AccessPoint& b) { return a.strength > b.strength; });
  return aps;
}

std::map<std::string, std::string> parse_key_values(const std::string& text) {
  std::map<std::string, std::string> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    const size_t eq = line.find('=');
    if (eq != std::string::npos) out[line.substr(0, eq)] = line.substr(eq + 1);
  }
  return out;
}

std::vector<WpaNetwork> parse_wpa_list_networks(const std::string& text) {
  std::vector<WpaNetwork> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    std::vector<std::string> f;
    std::stringstream ls(line);
    std::string cell;
    while (std::getline(ls, cell, '\t')) f.push_back(cell);
    if (f.size() < 2 || f[0] == "network id / ssid / bssid / flags") continue;
    WpaNetwork n;
    n.id = std::atoi(f[0].c_str());
    n.ssid = unescape_wpa_ssid(f[1]);
    n.current = f.size() >= 4 && f[3].find("[CURRENT]") != std::string::npos;
    out.push_back(n);
  }
  return out;
}

std::string format_speed(int mbps) {
  if (mbps <= 0) return "";
  if (mbps >= 1000) {
    char buf[32];
    if (mbps % 1000 == 0) std::snprintf(buf, sizeof buf, "%d Gbit/s", mbps / 1000);
    else std::snprintf(buf, sizeof buf, "%.1f Gbit/s", mbps / 1000.0);
    return buf;
  }
  return std::to_string(mbps) + " Mbit/s";
}

const char* state_label(State s) {
  switch (s) {
    case State::Connected: return "Connected";
    case State::Connecting: return "Connecting...";
    case State::Disconnected: return "Not connected";
    default: return "Unavailable";
  }
}

std::string describe_device(const Device& d) {
  std::string out;
  if (d.kind == Kind::Ethernet) {
    if (d.state == State::Unavailable) return "Cable unplugged";
    out = state_label(d.state);
    if (d.state == State::Connected && !format_speed(d.speed_mbps).empty()) out += ", " + format_speed(d.speed_mbps);
    return out;
  }
  if (d.state == State::Connected) {
    out = "Connected";
    if (!d.connection.empty()) out += " to " + d.connection;
    if (d.signal > 0) out += " (" + std::to_string(d.signal) + "%)";
    return out;
  }
  if (d.state == State::Connecting) return d.connection.empty() ? "Connecting..." : "Connecting to " + d.connection + "...";
  if (d.state == State::Unavailable) return "Wi-Fi is off or the card is not ready";
  return "Not connected";
}

const Device* primary_device(const std::vector<Device>& devices) {
  const Device* wifi = nullptr;
  const Device* first = devices.empty() ? nullptr : &devices.front();
  for (const Device& d : devices) {
    if (d.state == State::Connected && d.kind == Kind::Ethernet) return &d;
    if (d.state == State::Connected && d.kind == Kind::Wifi && !wifi) wifi = &d;
  }
  if (wifi) return wifi;
  for (const Device& d : devices)
    if (d.state == State::Connecting) return &d;
  for (const Device& d : devices)
    if (d.kind == Kind::Wifi) return &d;
  return first;
}

}  // namespace fleetwm::net
