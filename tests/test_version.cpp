#include <gtest/gtest.h>

#include <cctype>

#include "version.hpp"

using namespace fleetwm;

TEST(Version, NumberLooksLikeMajorMinorPatch) {
  const std::string v = version_number();
  ASSERT_FALSE(v.empty());
  int dots = 0;
  for (char c : v) {
    if (c == '.') ++dots;
    else EXPECT_TRUE(std::isdigit(static_cast<unsigned char>(c))) << v;
  }
  EXPECT_EQ(dots, 2) << v;
}

TEST(Version, RevisionIsNeverEmpty) {
  EXPECT_FALSE(version_revision().empty());  // a hash, or "unknown" outside a git checkout
}

TEST(Version, StringStartsWithTheNumber) {
  EXPECT_EQ(version_string().rfind(version_number(), 0), 0u);
}

TEST(Version, StringShowsTheRevisionInBrackets) {
  const std::string rev = version_revision();
  if (rev == "unknown") {
    EXPECT_EQ(version_string(), version_number());
  } else {
    EXPECT_EQ(version_string(), version_number() + " (" + rev + ")");
  }
}
