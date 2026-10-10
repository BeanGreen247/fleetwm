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
