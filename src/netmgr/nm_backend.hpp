#pragma once

#include <memory>

#include "network_types.hpp"

namespace fleetwm::net {

// NetworkManager over the system D-Bus (the manager GNOME, KDE, XFCE and most distributions
// use). Null when NetworkManager is not running.
std::unique_ptr<Backend> make_nm_backend();

}  // namespace fleetwm::net
