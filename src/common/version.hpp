#pragma once

#include <cstdio>
#include <cstring>
#include <string>

#include "version_config.h"

#ifndef FLEETWM_VERSION
#define FLEETWM_VERSION "0.0.0"
#endif

namespace fleetwm {

// "0.3.0" -- the project version from meson.build.
inline std::string version_number() { return FLEETWM_VERSION; }

// "89ac1fc" -- the git revision this build came from.
inline std::string version_revision() { return FLEETWM_GIT_REVISION; }

// "0.3.0 (89ac1fc)" -- what Settings -> About and `fleetwm --version` show.
inline std::string version_string() {
  const std::string rev = version_revision();
  return version_number() + (rev.empty() || rev == "unknown" ? "" : " (" + rev + ")");
}

// Who made it and where it lives, shown by every program's --version and --help.
inline const char* credit_text() {
  return "Copyright (c) 2026 Thomas Mozdren\n"
         "https://github.com/BeanGreen247/fleetwm\n";
}

// For a program's main(): prints the version (--version, -V) or the usage (--help, -h) followed by the credit and returns true, in which
// case main() should return 0. `usage` is the argument synopsis after the program name ("" when it takes none).
inline bool handle_info_flags(int argc, char** argv, const char* program, const char* usage = "") {
  if (argc < 2) return false;
  if (std::strcmp(argv[1], "--version") == 0 || std::strcmp(argv[1], "-V") == 0) {
    std::printf("%s %s\n%s", program, version_string().c_str(), credit_text());
    return true;
  }
  if (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) {
    std::printf("Usage: %s%s%s\n\n%s %s\n%s", program, *usage ? " " : "", usage, program, version_string().c_str(), credit_text());
    return true;
  }
  return false;
}

}  // namespace fleetwm
