#include <gtest/gtest.h>

#include "terminal_launch.hpp"

using namespace fleetwm;
using ArgList = std::vector<std::string>;

TEST(TerminalArgv, PlainFootUsesTheBundledConfig) {
  EXPECT_EQ(terminal_argv("foot", "/etc/fleetwm", false, true), (ArgList{"foot", "-c", "/etc/fleetwm/foot.ini"}));
}

TEST(TerminalArgv, AUsersOwnFootConfigWins) {
  EXPECT_EQ(terminal_argv("foot", "/etc/fleetwm", true, true), (ArgList{"foot"}));
}

TEST(TerminalArgv, MissingBundledConfigFallsBackToPlainFoot) {
  EXPECT_EQ(terminal_argv("foot", "/etc/fleetwm", false, false), (ArgList{"foot"}));
}

TEST(TerminalArgv, OtherTerminalsAreLeftAlone) {
  EXPECT_EQ(terminal_argv("alacritty", "/etc/fleetwm", false, true), (ArgList{"alacritty"}));
  EXPECT_EQ(terminal_argv("foot --server", "/etc/fleetwm", false, true), (ArgList{"foot --server"}));
}
