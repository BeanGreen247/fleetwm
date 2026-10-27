#include "window_cycle.hpp"

#include <gtest/gtest.h>

#include <string>

namespace fleetwm {
namespace {

using V = std::vector<std::string>;

TEST(WindowCycle, OneStepSwapsTheTopTwo) {
  EXPECT_EQ(window_cycle_order<std::string>({"A", "B", "C", "D"}, "B"), (V{"B", "A", "C", "D"}));
}

TEST(WindowCycle, SteppingFurtherKeepsTheSkippedWindowsInPlace) {
  EXPECT_EQ(window_cycle_order<std::string>({"A", "B", "C", "D"}, "D"), (V{"D", "A", "B", "C"}));
}

TEST(WindowCycle, TwoTapsGoBackAndForth) {
  V order = window_cycle_order<std::string>({"A", "B", "C"}, "B");
  order = window_cycle_order<std::string>(order, "A");
  EXPECT_EQ(order, (V{"A", "B", "C"}));
}

TEST(WindowCycle, ChoosingTheCurrentWindowChangesNothing) {
  EXPECT_EQ(window_cycle_order<std::string>({"A", "B"}, "A"), (V{"A", "B"}));
}

}  // namespace
}  // namespace fleetwm
