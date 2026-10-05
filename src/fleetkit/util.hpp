#pragma once

// Small string helpers shared by fleetkit's translation units (defined inline in
// one place because unity builds merge the .cpp files into one TU, where two
// anonymous-namespace copies would collide).

#include <sstream>
#include <string>
#include <vector>

namespace fleetwm::kit::detail {

inline std::string trim(const std::string& s) {
  const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

inline std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, sep)) {
    item = trim(item);
    if (!item.empty()) out.push_back(item);
  }
  return out;
}

}  // namespace fleetwm::kit::detail
