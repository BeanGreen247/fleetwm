#include "osd_logic.hpp"

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

using namespace fleetwm::osd;
namespace fs = std::filesystem;

namespace {
void osd_put(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << s;
}
}  // namespace

TEST(OsdLogic, ParsesTheCommandLines) {
  Command c;
  ASSERT_TRUE(parse_command("volume up", &c));
  EXPECT_EQ(c.kind, Kind::Volume);
  EXPECT_EQ(c.action, Action::Up);
  ASSERT_TRUE(parse_command("volume mute\n", &c));
  EXPECT_EQ(c.action, Action::Mute);
  ASSERT_TRUE(parse_command("brightness down\r\n", &c));
  EXPECT_EQ(c.kind, Kind::Brightness);
  EXPECT_EQ(c.action, Action::Down);
  ASSERT_TRUE(parse_command("brightness show", &c));
  EXPECT_EQ(c.action, Action::Show);
}

TEST(OsdLogic, RejectsWhatIsNotACommand) {
  Command c;
  EXPECT_FALSE(parse_command("", &c));
  EXPECT_FALSE(parse_command("volume", &c));
  EXPECT_FALSE(parse_command("brightness mute", &c));
  EXPECT_FALSE(parse_command("balance up", &c));
  EXPECT_FALSE(parse_command("volume loud", &c));
}

TEST(OsdLogic, StepsToTheNextMultipleAndStaysInRange) {
  EXPECT_EQ(step_percent(0, true), 5);
  EXPECT_EQ(step_percent(47, true), 50);
  EXPECT_EQ(step_percent(50, true), 55);
  EXPECT_EQ(step_percent(98, true), 100);
  EXPECT_EQ(step_percent(100, true), 100);
  EXPECT_EQ(step_percent(50, false), 45);
  EXPECT_EQ(step_percent(47, false), 45);
  EXPECT_EQ(step_percent(3, false), 0);
  EXPECT_EQ(step_percent(0, false), 0);
  EXPECT_EQ(step_percent(150, false), 95) << "an over-range level is clamped first";
  EXPECT_EQ(step_percent(-5, true), 5);
}

TEST(OsdLogic, ReadsWpctlVolume) {
  int p = -1;
  bool m = true;
  ASSERT_TRUE(parse_wpctl_volume("Volume: 0.45\n", &p, &m));
  EXPECT_EQ(p, 45);
  EXPECT_FALSE(m);
  ASSERT_TRUE(parse_wpctl_volume("Volume: 0.30 [MUTED]\n", &p, &m));
  EXPECT_EQ(p, 30);
  EXPECT_TRUE(m);
  ASSERT_TRUE(parse_wpctl_volume("Volume: 1.00\n", &p, &m));
  EXPECT_EQ(p, 100);
  EXPECT_FALSE(parse_wpctl_volume("", &p, &m));
  EXPECT_FALSE(parse_wpctl_volume("Could not connect", &p, &m));
}

TEST(OsdLogic, ReadsTheLargestBacklight) {
  const fs::path d = fs::temp_directory_path() / ("fleetwm-osd-" + std::to_string(getpid()));
  fs::remove_all(d);
  osd_put(d / "acpi_video0/brightness", "5\n");
  osd_put(d / "acpi_video0/max_brightness", "10\n");
  osd_put(d / "intel_backlight/brightness", "480\n");
  osd_put(d / "intel_backlight/max_brightness", "960\n");
  osd_put(d / "broken/max_brightness", "0\n");
  Backlight b;
  ASSERT_TRUE(read_backlight(d.string(), &b));
  EXPECT_EQ(b.name, "intel_backlight");
  EXPECT_EQ(b.current, 480);
  EXPECT_EQ(b.max, 960);
  EXPECT_EQ(backlight_percent(b.current, b.max), 50);
  fs::remove_all(d);
  EXPECT_FALSE(read_backlight((d / "absent").string(), &b));
}

TEST(OsdLogic, BacklightValuesNeverGoBlack) {
  EXPECT_EQ(backlight_value(0, 960), 1);
  EXPECT_EQ(backlight_value(50, 960), 480);
  EXPECT_EQ(backlight_value(100, 960), 960);
  EXPECT_EQ(backlight_value(150, 960), 960);
  EXPECT_EQ(backlight_value(50, 0), 0);
  EXPECT_EQ(backlight_percent(1, 0), 0);
  EXPECT_EQ(backlight_percent(2000, 960), 100);
}

TEST(OsdLogic, LitSegmentsOfTheBar) {
  EXPECT_EQ(lit_segments(0), 0);
  EXPECT_EQ(lit_segments(1), 1) << "any level above zero shows one segment";
  EXPECT_EQ(lit_segments(50), 8);
  EXPECT_EQ(lit_segments(100), 16);
  EXPECT_EQ(lit_segments(140), 16);
  EXPECT_EQ(lit_segments(-3), 0);
  EXPECT_EQ(lit_segments(50, 10), 5);
}
