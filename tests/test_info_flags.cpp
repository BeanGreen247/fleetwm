#include <gtest/gtest.h>

#include <string>

#include "version.hpp"

namespace fleetwm {
namespace {

std::string run(const char* flag, const char* usage, bool* handled) {
  char prog[] = "fleetwm-test";
  char arg[32];
  std::snprintf(arg, sizeof arg, "%s", flag);
  char* argv[] = {prog, arg, nullptr};
  testing::internal::CaptureStdout();
  *handled = handle_info_flags(2, argv, "fleetwm-test", usage);
  return testing::internal::GetCapturedStdout();
}

TEST(InfoFlags, VersionAndHelpNameTheAuthorAndTheProject) {
  for (const char* flag : {"--version", "-V", "--help", "-h"}) {
    bool handled = false;
    const std::string out = run(flag, "[--output NAME]", &handled);
    EXPECT_TRUE(handled) << flag;
    EXPECT_NE(out.find("Thomas Mozdren"), std::string::npos) << flag;
    EXPECT_NE(out.find("2026"), std::string::npos) << flag;
    EXPECT_NE(out.find("https://github.com/BeanGreen247/fleetwm"), std::string::npos) << flag;
    EXPECT_NE(out.find(version_string()), std::string::npos) << flag;
  }
}

TEST(InfoFlags, HelpShowsTheUsageLine) {
  bool handled = false;
  EXPECT_NE(run("--help", "[--output NAME]", &handled).find("Usage: fleetwm-test [--output NAME]"), std::string::npos);
  EXPECT_NE(run("-h", "", &handled).find("Usage: fleetwm-test\n"), std::string::npos);
}

TEST(InfoFlags, OtherArgumentsAreLeftAlone) {
  char prog[] = "fleetwm-test", a[] = "--output", b[] = "DP-1";
  char* argv[] = {prog, a, b, nullptr};
  EXPECT_FALSE(handle_info_flags(3, argv, "fleetwm-test"));
  EXPECT_FALSE(handle_info_flags(1, argv, "fleetwm-test"));
}

}  // namespace
}  // namespace fleetwm
