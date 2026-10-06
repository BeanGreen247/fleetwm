#pragma once

#include <string>
#include <vector>

#include "network_types.hpp"

namespace fleetwm::net {

// The physical network devices as the kernel sees them (no daemon needed): Ethernet and Wi-Fi
// cards with their state, addresses, link speed and driver. Virtual devices (loopback, VPN,
// containers, bridges) are left out. `sys_net` is a parameter so tests can use a fake tree.
// Empty = /sys/class/net (or $FLEETWM_SYS_NET, a test hook).
std::vector<Device> read_system_devices(const std::string& sys_net = "");

// Fills in IPv4/IPv6 addresses and the default gateway of each device.
void fill_addresses(std::vector<Device>* devices);

// Read-only backend: shows what the kernel knows, cannot connect or scan.
class SystemStatusBackend : public Backend {
 public:
  Snapshot snapshot() override;
  void scan(const std::string&) override {}
  bool connect(const std::string&, const std::string&, const std::string&, std::string* error) override;
  bool disconnect(const std::string&, std::string* error) override;
  bool forget(const std::string&, const std::string&, std::string* error) override;
};

std::string read_text_file(const std::string& path);

}  // namespace fleetwm::net
