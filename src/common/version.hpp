#pragma once

#include <string>

#include "version_config.h"

#ifndef FLEETWM_VERSION
#define FLEETWM_VERSION "0.0.0"
#endif

namespace fleetwm {

// "0.1.0" -- the project version from meson.build.
inline std::string version_number() { return FLEETWM_VERSION; }

// "89ac1fc" -- the git revision this build came from.
inline std::string version_revision() { return FLEETWM_GIT_REVISION; }

// "0.1.0 (89ac1fc)" -- what Settings -> About and `fleetwm --version` show.
inline std::string version_string() {
  const std::string rev = version_revision();
  return version_number() + (rev.empty() || rev == "unknown" ? "" : " (" + rev + ")");
}

}  // namespace fleetwm
