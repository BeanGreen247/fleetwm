#include <gtest/gtest.h>

#include "test_util.hpp"
#include "user_profile.hpp"

using namespace fleetwm;
using ProfileTest = testutil::ScopedConfigHome;

TEST_F(ProfileTest, DefaultsToAUsefulDisplayName) {
  EXPECT_FALSE(load_user_profile().display_name.empty());
}

TEST_F(ProfileTest, RoundTripsDisplayNameAndIconWithoutChangingUsername) {
  const UserProfile input{"Bean's Laptop", "/tmp/profile.png"};
  save_user_profile(input);
  EXPECT_EQ(load_user_profile(), input);
}

TEST(UserProfileInitials, OneLetterForOneNameTwoForNameAndSurname) {
  EXPECT_EQ(fleetwm::profile_initials("thomas"), "T");
  EXPECT_EQ(fleetwm::profile_initials("Thomas Mozdren"), "TM");
  EXPECT_EQ(fleetwm::profile_initials("thomas mozdren"), "TM");
  EXPECT_EQ(fleetwm::profile_initials("  Anna  Maria  Novak "), "AN");  // first and last word
  EXPECT_EQ(fleetwm::profile_initials(""), "F");
  EXPECT_EQ(fleetwm::profile_initials("123 !!"), "F");
  EXPECT_EQ(fleetwm::profile_initials("_bean"), "B");
  EXPECT_EQ(fleetwm::profile_initials("Šárka Nováková"), "ŠN");
}
