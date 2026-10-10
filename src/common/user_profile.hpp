#pragma once

#include <string>

namespace fleetwm {

struct UserProfile {
  std::string display_name;
  std::string icon_path;
  bool operator==(const UserProfile&) const = default;
};

std::string user_profile_path();
UserProfile load_user_profile();
void save_user_profile(const UserProfile& profile);

}  // namespace fleetwm
