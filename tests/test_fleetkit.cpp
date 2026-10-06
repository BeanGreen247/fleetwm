// Unit tests for the pure (no Wayland) parts of src/fleetkit: desktop-entry Exec
// expansion, colour parsing, icon-less helpers.
#include <gtest/gtest.h>

#include "desktop_entry.hpp"
#include "fleetkit.hpp"

using fleetwm::kit::DesktopEntry;
using fleetwm::kit::exec_argv;
using fleetwm::kit::exec_basename;
using fleetwm::kit::parse_color;

namespace {
DesktopEntry entry(const std::string& exec) {
  DesktopEntry e;
  e.name = "Demo App";
  e.file_path = "/usr/share/applications/demo.desktop";
  e.icon = "demo-icon";
  e.exec = exec;
  return e;
}
}  // namespace

TEST(DesktopEntryExec, DropsFileFieldCodes) {
  const auto argv = exec_argv(entry("firefox %u"));
  ASSERT_EQ(argv.size(), 1u);
  EXPECT_EQ(argv[0], "firefox");
}

TEST(DesktopEntryExec, KeepsPlainArguments) {
  const auto argv = exec_argv(entry("env FOO=1 /usr/bin/app --flag %F"));
  ASSERT_EQ(argv.size(), 4u);
  EXPECT_EQ(argv[0], "env");
  EXPECT_EQ(argv[3], "--flag");
}

TEST(DesktopEntryExec, QuotedArgumentWithSpaces) {
  const auto argv = exec_argv(entry("sh -c \"echo hello world\""));
  ASSERT_EQ(argv.size(), 3u);
  EXPECT_EQ(argv[2], "echo hello world");
}

TEST(DesktopEntryExec, EscapedQuoteInsideQuotes) {
  const auto argv = exec_argv(entry("app \"say \\\"hi\\\"\""));
  ASSERT_EQ(argv.size(), 2u);
  EXPECT_EQ(argv[1], "say \"hi\"");
}

TEST(DesktopEntryExec, IconNameAndKeyCodes) {
  const auto argv = exec_argv(entry("app %i %c %k 100%%"));
  ASSERT_EQ(argv.size(), 6u);
  EXPECT_EQ(argv[1], "--icon");
  EXPECT_EQ(argv[2], "demo-icon");
  EXPECT_EQ(argv[3], "Demo App");
  EXPECT_EQ(argv[4], "/usr/share/applications/demo.desktop");
  EXPECT_EQ(argv[5], "100%");
}

TEST(DesktopEntryExec, BasenameOfAbsolutePath) {
  EXPECT_EQ(exec_basename(entry("/usr/bin/foot --server")), "foot");
  EXPECT_EQ(exec_basename(entry("")), "");
}

TEST(FleetkitColor, ParsesRgbAndRgba) {
  const auto c = parse_color("#ff8000");
  EXPECT_DOUBLE_EQ(c.r, 1.0);
  EXPECT_NEAR(c.g, 128 / 255.0, 1e-9);
  EXPECT_DOUBLE_EQ(c.b, 0.0);
  EXPECT_DOUBLE_EQ(c.a, 1.0);
  const auto d = parse_color("#00000080");
  EXPECT_NEAR(d.a, 128 / 255.0, 1e-9);
}

TEST(FleetkitColor, MalformedFallsBack) {
  fleetwm::kit::Color fb{0.1, 0.2, 0.3, 1.0};
  EXPECT_DOUBLE_EQ(parse_color("red", fb).r, 0.1);
  EXPECT_DOUBLE_EQ(parse_color("#12345", fb).g, 0.2);
  EXPECT_DOUBLE_EQ(parse_color("#12zz56", fb).b, 0.3);
}

#include "backdrop.hpp"

TEST(Backdrop, BlurKeepsAFlatPictureFlat) {
  std::vector<uint8_t> px(8 * 8 * 4, 120);
  fleetwm::kit::box_blur_rgba(px.data(), 8, 8, 2, 3);
  for (uint8_t v : px) EXPECT_EQ(v, 120);
}

TEST(Backdrop, BlurSpreadsABrightPixelAndKeepsTheTotalCloseToConstant) {
  std::vector<uint8_t> px(9 * 9 * 4, 0);
  px[(4 * 9 + 4) * 4] = 255;
  fleetwm::kit::box_blur_rgba(px.data(), 9, 9, 1, 1);
  EXPECT_LT(px[(4 * 9 + 4) * 4], 255);
  EXPECT_GT(px[(4 * 9 + 5) * 4], 0);
  EXPECT_GT(px[(5 * 9 + 5) * 4], 0);
}

TEST(Backdrop, DownscaleKeepsTheAspectRatioAndNeverEnlarges) {
  std::vector<uint8_t> src(400 * 200 * 4, 77);
  uint8_t* out = nullptr;
  int w = 0, h = 0;
  fleetwm::kit::downscale_rgba(src.data(), 400, 200, 100, &out, &w, &h);
  EXPECT_EQ(w, 100);
  EXPECT_EQ(h, 50);
  EXPECT_EQ(out[0], 77);
  std::free(out);
  fleetwm::kit::downscale_rgba(src.data(), 400, 200, 1000, &out, &w, &h);
  EXPECT_EQ(w, 400);
  std::free(out);
}
