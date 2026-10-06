#include "nm_backend.hpp"
#include "system_devices.hpp"
#include "wpa_backend.hpp"

namespace fleetwm::net {

std::unique_ptr<Backend> make_backend() {
  if (auto nm = make_nm_backend()) return nm;
  if (wpa_available()) return make_wpa_backend();
  return std::make_unique<SystemStatusBackend>();
}

}  // namespace fleetwm::net
