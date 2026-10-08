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
  EXPECT_EQ(terminal_argv("foot --server", "/etc/fleetwm", false, true), (ArgList{"foot", "--server"}));
}

TEST(TerminalArgv, ACommandWithArgumentsIsSplitIntoWords) {
  EXPECT_EQ(terminal_argv("/usr/local/bin/lestrix --lite", "/etc/fleetwm", false, true), (ArgList{"/usr/local/bin/lestrix", "--lite"}));
}

TEST(SplitCommand, WordsQuotesAndEscapes) {
  EXPECT_EQ(split_command("a  b\tc"), (ArgList{"a", "b", "c"}));
  EXPECT_EQ(split_command("sh -c 'echo hi there'"), (ArgList{"sh", "-c", "echo hi there"}));
  EXPECT_EQ(split_command("x \"a b\" c\\ d"), (ArgList{"x", "a b", "c d"}));
  EXPECT_EQ(split_command("e ''"), (ArgList{"e", ""}));
  EXPECT_TRUE(split_command("   ").empty());
}

TEST(SplitCommand, JoinThenSplitGivesTheSameWords) {
  for (const ArgList& words : {ArgList{"lestrix", "--lite"}, ArgList{"/usr/bin/t", "a b", "it's", "", "x\"y", "$HOME"}, ArgList{"foot"}})
    EXPECT_EQ(split_command(join_command(words)), words) << join_command(words);
  EXPECT_EQ(join_command({"/usr/local/bin/lestrix", "--lite"}), "/usr/local/bin/lestrix --lite");
}
