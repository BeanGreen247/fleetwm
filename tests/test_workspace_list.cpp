#include <gtest/gtest.h>

#include "workspace_list.hpp"

using namespace fleetwm;
using V = std::vector<int>;

TEST(WorkspaceList, StartsWithTheMinimum) {
  EXPECT_EQ(visible_workspaces({}, 0, 4), (V{0, 1, 2, 3}));
}

TEST(WorkspaceList, AWindowOnSixAddsSix) {
  EXPECT_EQ(visible_workspaces({5}, 0, 4), (V{0, 1, 2, 3, 5}));
}

TEST(WorkspaceList, StaysInNumericalOrder) {
  EXPECT_EQ(visible_workspaces({5, 4}, 0, 4), (V{0, 1, 2, 3, 4, 5}));
  EXPECT_EQ(visible_workspaces({8, 4, 5}, 0, 4), (V{0, 1, 2, 3, 4, 5, 8}));
}

TEST(WorkspaceList, EmptyOnesDisappearWhenLeft) {
  // workspaces 5 and 6 (indices 4, 5) were closed, 9 still has a window
  EXPECT_EQ(visible_workspaces({8}, 0, 4), (V{0, 1, 2, 3, 8}));
}

TEST(WorkspaceList, TheCurrentWorkspaceIsShownEvenWhenEmpty) {
  EXPECT_EQ(visible_workspaces({}, 6, 4), (V{0, 1, 2, 3, 6}));
}

TEST(WorkspaceList, DuplicatesAndOutOfRangeAreIgnored) {
  EXPECT_EQ(visible_workspaces({5, 5, 5, -1, 99}, 0, 4), (V{0, 1, 2, 3, 5}));
}

TEST(WorkspaceList, MinimumIsClamped) {
  EXPECT_EQ(visible_workspaces({}, 0, 0), (V{0}));
  EXPECT_EQ(visible_workspaces({}, 0, 50).size(), 10u);
}
