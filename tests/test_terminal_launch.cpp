#include <gtest/gtest.h>

#include "terminal_launch.hpp"

using namespace fleetwm;
using V = std::vector<std::string>;

TEST(TerminalArgv, PlainFootUsesTheBundledConfig) {
  EXPECT_EQ(terminal_argv("foot", "/etc/fleetwm", false, true), (V{"foot", "-c", "/etc/fleetwm/foot.ini"}));
}

TEST(TerminalArgv, AUsersOwnFootConfigWins) {
  EXPECT_EQ(terminal_argv("foot", "/etc/fleetwm", true, true), (V{"foot"}));
}

TEST(TerminalArgv, MissingBundledConfigFallsBackToPlainFoot) {
  EXPECT_EQ(terminal_argv("foot", "/etc/fleetwm", false, false), (V{"foot"}));
}

TEST(TerminalArgv, OtherTerminalsAreLeftAlone) {
  EXPECT_EQ(terminal_argv("alacritty", "/etc/fleetwm", false, true), (V{"alacritty"}));
  EXPECT_EQ(terminal_argv("foot --server", "/etc/fleetwm", false, true), (V{"foot --server"}));
}
