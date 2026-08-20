#include "config_paths.hpp"

#include <cstdlib>
#include <stdexcept>

namespace fleetwm::config_internal {

std::filesystem::path config_home() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
    return std::filesystem::path(xdg);
  }
  const char* home = std::getenv("HOME");
  if (!home) {
    throw std::runtime_error("HOME is not set; cannot resolve config path");
  }
  return std::filesystem::path(home) / ".config";
}

}  // namespace fleetwm::config_internal
