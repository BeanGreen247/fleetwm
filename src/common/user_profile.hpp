#pragma once

#include <string>

namespace fleetwm {

struct UserProfile {
  std::string display_name;
  std::string icon_path;
  bool operator==(const UserProfile&) const = default;
};

// The letters of the round profile picture: one letter when the name is a single word, the first letter of the first and of the last word when it
// has a name and a surname ("Thomas Mozdren" gives "TM"); "F" when the name has no letter at all. ASCII letters are capitalised, any other letter is kept.
std::string profile_initials(const std::string& display_name);

std::string user_profile_path();
UserProfile load_user_profile();
void save_user_profile(const UserProfile& profile);

}  // namespace fleetwm
