#pragma once

#include <memory>
#include <string>

#include "network_types.hpp"

namespace fleetwm::net {

// Talks to a running wpa_supplicant over its control sockets (/run/wpa_supplicant/<card>),
// the way systemd-networkd, netplan and ifupdown setups handle Wi-Fi. `ctrl_dir` is a
// parameter so tests can run a fake supplicant.
// Empty arguments mean the real directories ($FLEETWM_WPA_DIR / $FLEETWM_SYS_NET are test hooks).
std::unique_ptr<Backend> make_wpa_backend(const std::string& ctrl_dir = "", const std::string& sys_net = "");

// True when ctrl_dir has a control socket we may use.
bool wpa_available(const std::string& ctrl_dir = "");

}  // namespace fleetwm::net
