#include "system_devices.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "network_parse.hpp"

namespace fleetwm::net {

namespace fs = std::filesystem;

std::string read_text_file(const std::string& path) {
  std::ifstream f(path);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

namespace {
std::string trim(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\r')) s.pop_back();
  return s;
}
}  // namespace

std::vector<Device> read_system_devices(const std::string& sys_net_arg) {
  std::vector<Device> out;
  const char* env = std::getenv("FLEETWM_SYS_NET");
  const std::string sys_net = !sys_net_arg.empty() ? sys_net_arg : env && *env ? env : "/sys/class/net";
  std::error_code ec;
  const std::map<std::string, int> quality = parse_proc_wireless(read_text_file("/proc/net/wireless"));
  for (const auto& entry : fs::directory_iterator(sys_net, ec)) {
    const std::string name = entry.path().filename().string();
    const fs::path dir = entry.path();
    if (!fs::exists(dir / "device", ec)) continue;  // virtual: loopback, tunnels, bridges, containers
    const bool wifi = fs::exists(dir / "wireless", ec) || fs::exists(dir / "phy80211", ec);
    if (!wifi && trim(read_text_file((dir / "type").string())) != "1") continue;  // not Ethernet-like
    Device d;
    d.name = name;
    d.kind = wifi ? Kind::Wifi : Kind::Ethernet;
    d.hw_addr = trim(read_text_file((dir / "address").string()));
    const fs::path drv = fs::read_symlink(dir / "device" / "driver", ec);
    if (!ec) d.driver = drv.filename().string();
    const std::string oper = trim(read_text_file((dir / "operstate").string()));
    const std::string carrier = trim(read_text_file((dir / "carrier").string()));
    if (oper == "up") d.state = State::Connected;
    else if (oper == "dormant") d.state = State::Connecting;
    else if (!wifi && carrier != "1") d.state = State::Unavailable;  // cable unplugged
    else d.state = State::Disconnected;
    if (!wifi) {
      const int speed = std::atoi(trim(read_text_file((dir / "speed").string())).c_str());
      d.speed_mbps = speed > 0 ? speed : 0;
    } else if (auto q = quality.find(name); q != quality.end()) {
      d.signal = q->second;
    }
    out.push_back(std::move(d));
  }
  std::sort(out.begin(), out.end(), [](const Device& a, const Device& b) {
    if (a.kind != b.kind) return a.kind == Kind::Ethernet;  // Ethernet first
    return a.name < b.name;
  });
  return out;
}

void fill_addresses(std::vector<Device>* devices) {
  ifaddrs* list = nullptr;
  if (getifaddrs(&list) != 0) return;
  const std::string route = read_text_file("/proc/net/route");
  for (Device& d : *devices) {
    for (ifaddrs* a = list; a; a = a->ifa_next) {
      if (!a->ifa_addr || d.name != a->ifa_name) continue;
      char buf[INET6_ADDRSTRLEN] = {};
      int prefix = 0;
      auto prefix_of = [](const sockaddr* mask, int bytes_off, int len) {
        int bits = 0;
        const unsigned char* p = reinterpret_cast<const unsigned char*>(mask) + bytes_off;
        for (int i = 0; i < len; ++i)
          for (int b = 7; b >= 0; --b) bits += (p[i] >> b) & 1;
        return bits;
      };
      if (a->ifa_addr->sa_family == AF_INET) {
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr, buf, sizeof buf);
        if (a->ifa_netmask) prefix = prefix_of(a->ifa_netmask, offsetof(sockaddr_in, sin_addr), 4);
      } else if (a->ifa_addr->sa_family == AF_INET6) {
        auto* in6 = reinterpret_cast<sockaddr_in6*>(a->ifa_addr);
        if (IN6_IS_ADDR_LINKLOCAL(&in6->sin6_addr)) continue;  // fe80:: is noise for the user
        inet_ntop(AF_INET6, &in6->sin6_addr, buf, sizeof buf);
        if (a->ifa_netmask) prefix = prefix_of(a->ifa_netmask, offsetof(sockaddr_in6, sin6_addr), 16);
      } else {
        continue;
      }
      d.addresses.push_back(std::string(buf) + "/" + std::to_string(prefix));
    }
    d.gateway = parse_default_gateway(route, d.name);
  }
  freeifaddrs(list);
}

Snapshot SystemStatusBackend::snapshot() {
  Snapshot s;
  s.backend = "System status only";
  s.devices = read_system_devices();
  fill_addresses(&s.devices);
  for (Device& d : s.devices) d.controllable = false;
  bool wifi = false;
  for (const Device& d : s.devices) wifi |= d.kind == Kind::Wifi;
  s.note = wifi ? "No network manager found, so Wi-Fi networks cannot be listed or joined here. "
                  "Install NetworkManager or start wpa_supplicant."
                : "";
  return s;
}

bool SystemStatusBackend::connect(const std::string&, const std::string&, const std::string&, std::string* error) {
  if (error) *error = "no network manager is running";
  return false;
}
bool SystemStatusBackend::disconnect(const std::string&, std::string* error) {
  if (error) *error = "no network manager is running";
  return false;
}
bool SystemStatusBackend::forget(const std::string&, const std::string&, std::string* error) {
  if (error) *error = "no network manager is running";
  return false;
}

}  // namespace fleetwm::net
