#include "battery_reading.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fleetwm {
namespace {

// Exercises battery_internal::read_battery_reading() -- the pure
// sysfs-parsing/rate-math logic BatterySource::poll_once() delegates to --
// against a fake directory of files standing in for /sys/class/power_supply
// /BATn, rather than real battery hardware (none is present on
// fleetwm-dev, and CI/dev machines vary).
class BatterySourceReadingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("fleetwm-battery-test-" +
            std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
            std::to_string(reinterpret_cast<uintptr_t>(this)));
    std::filesystem::create_directories(dir_);
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }

  void write(const std::string& name, const std::string& contents) {
    std::ofstream(dir_ / name) << contents;
  }

  std::string path() const { return dir_.string(); }

  std::filesystem::path dir_;
};

TEST_F(BatterySourceReadingTest, EmptyDirArgMeansNoBattery) {
  BatteryReading reading = battery_internal::read_battery_reading("");
  EXPECT_FALSE(reading.available);
  EXPECT_EQ(reading.percent, 0);
  EXPECT_FALSE(reading.charging);
  EXPECT_EQ(reading.hours_remaining, -1.0);
}

TEST_F(BatterySourceReadingTest, MissingCapacityFileMeansUnavailable) {
  // Directory exists but has none of the expected files -- e.g. the
  // battery vanished mid-poll (hot-unplug) after find_battery_dir() ran.
  BatteryReading reading = battery_internal::read_battery_reading(path());
  EXPECT_FALSE(reading.available);
}

TEST_F(BatterySourceReadingTest, DiscoveringPlainCapacityAndStatus) {
  write("capacity", "73");
  write("status", "Discharging");
  BatteryReading reading = battery_internal::read_battery_reading(path());
  EXPECT_TRUE(reading.available);
  EXPECT_EQ(reading.percent, 73);
  EXPECT_FALSE(reading.charging);
}

TEST_F(BatterySourceReadingTest, ChargingStatusDetected) {
  write("capacity", "50");
  write("status", "Charging");
  BatteryReading reading = battery_internal::read_battery_reading(path());
  EXPECT_TRUE(reading.charging);
}

TEST_F(BatterySourceReadingTest, MissingStatusFileMeansNotCharging) {
  write("capacity", "50");
  BatteryReading reading = battery_internal::read_battery_reading(path());
  EXPECT_TRUE(reading.available);
  EXPECT_FALSE(reading.charging);
}

TEST_F(BatterySourceReadingTest, FullStatusReportsZeroHoursRemaining) {
  write("capacity", "100");
  write("status", "Full");
  BatteryReading reading = battery_internal::read_battery_reading(path());
  EXPECT_DOUBLE_EQ(reading.hours_remaining, 0.0);
}

TEST_F(BatterySourceReadingTest, NoRateFilesMeansHoursRemainingUnknown) {
  write("capacity", "40");
  write("status", "Discharging");
  BatteryReading reading = battery_internal::read_battery_reading(path());
  EXPECT_EQ(reading.hours_remaining, -1.0);
}

TEST_F(BatterySourceReadingTest, ZeroRateMeansHoursRemainingUnknown) {
  write("capacity", "40");
  write("status", "Discharging");
  write("energy_now", "1000");
  write("power_now", "0");
  BatteryReading reading = battery_internal::read_battery_reading(path());
  EXPECT_EQ(reading.hours_remaining, -1.0);
}

TEST_F(BatterySourceReadingTest, DischargingEnergyBasedRateMath) {
  write("capacity", "40");
  write("status", "Discharging");
  write("energy_now", "2000");
  write("power_now", "1000");
  BatteryReading reading = battery_internal::read_battery_reading(path());
  EXPECT_DOUBLE_EQ(reading.hours_remaining, 2.0);
}

TEST_F(BatterySourceReadingTest, ChargingEnergyBasedRateMathNeedsFullValue) {
  write("capacity", "40");
  write("status", "Charging");
  write("energy_now", "2000");
  write("energy_full", "10000");
  write("power_now", "4000");
  BatteryReading reading = battery_internal::read_battery_reading(path());
  EXPECT_DOUBLE_EQ(reading.hours_remaining, 2.0);
}

TEST_F(BatterySourceReadingTest, ChargingWithoutFullValueMeansHoursUnknown) {
  // energy_full/charge_full missing (some drivers don't expose it) --
  // can't compute time-to-full without it.
  write("capacity", "40");
  write("status", "Charging");
  write("energy_now", "2000");
  write("power_now", "4000");
  BatteryReading reading = battery_internal::read_battery_reading(path());
  EXPECT_EQ(reading.hours_remaining, -1.0);
}

TEST_F(BatterySourceReadingTest, ChargeBasedFallbackWhenEnergyFilesAbsent) {
  // Some drivers only expose charge_*/current_* (µAh/µA), not
  // energy_*/power_* (µWh/µW) -- same ratio math applies either way.
  write("capacity", "40");
  write("status", "Discharging");
  write("charge_now", "3000");
  write("current_now", "1500");
  BatteryReading reading = battery_internal::read_battery_reading(path());
  EXPECT_DOUBLE_EQ(reading.hours_remaining, 2.0);
}

TEST_F(BatterySourceReadingTest, EnergyBasedPreferredOverChargeBasedWhenBothPresent) {
  write("capacity", "40");
  write("status", "Discharging");
  write("energy_now", "4000");
  write("power_now", "1000");
  write("charge_now", "999999");
  write("current_now", "1");
  BatteryReading reading = battery_internal::read_battery_reading(path());
  EXPECT_DOUBLE_EQ(reading.hours_remaining, 4.0);
}

TEST_F(BatterySourceReadingTest, CorruptCapacityFileMeansUnavailable) {
  write("capacity", "not-a-number");
  BatteryReading reading = battery_internal::read_battery_reading(path());
  EXPECT_FALSE(reading.available);
}

// find_battery_dir() / ac_online() against fake power-supply trees, so the result
// never depends on whether the machine running the tests is a laptop.
class PowerSupplyTreeTest : public BatterySourceReadingTest {
 protected:
  void add_supply(const std::string& name, const std::string& type, const std::string& online = "") {
    std::filesystem::create_directories(dir_ / name);
    std::ofstream(dir_ / name / "type") << type << "\n";
    if (!online.empty()) std::ofstream(dir_ / name / "online") << online << "\n";
  }
};

TEST_F(PowerSupplyTreeTest, MissingDirectoryMeansNoBattery) {
  EXPECT_EQ(find_battery_dir((dir_ / "does-not-exist").string()), "");
}

TEST_F(PowerSupplyTreeTest, EmptyDirectoryMeansNoBattery) {
  EXPECT_EQ(find_battery_dir(path()), "");
}

TEST_F(PowerSupplyTreeTest, OnlyAMainsAdapterMeansNoBattery) {
  add_supply("ADP1", "Mains", "1");
  EXPECT_EQ(find_battery_dir(path()), "");
}

TEST_F(PowerSupplyTreeTest, FindsABatteryEntry) {
  add_supply("ADP1", "Mains", "1");
  add_supply("BAT0", "Battery");
  EXPECT_EQ(find_battery_dir(path()), path() + "/BAT0");
}

TEST_F(PowerSupplyTreeTest, OtherBatteryNumbersAreFound) {
  add_supply("BAT1", "Battery");
  EXPECT_EQ(find_battery_dir(path()), path() + "/BAT1");
}

TEST_F(PowerSupplyTreeTest, NamesThatMerelyContainBatAreIgnored) {
  add_supply("hid-battery-0", "Battery");  // peripherals (mice, keyboards) are not the laptop battery
  EXPECT_EQ(find_battery_dir(path()), "");
}

TEST_F(PowerSupplyTreeTest, NoMainsSupplyMeansPluggedIn) {
  EXPECT_TRUE(ac_online(path()));                              // empty tree: desktop / VM
  EXPECT_TRUE(ac_online((dir_ / "does-not-exist").string()));  // no sysfs at all
}

TEST_F(PowerSupplyTreeTest, MainsOnlineIsPluggedIn) {
  add_supply("ADP1", "Mains", "1");
  add_supply("BAT0", "Battery");
  EXPECT_TRUE(ac_online(path()));
}

TEST_F(PowerSupplyTreeTest, MainsOfflineMeansOnBattery) {
  add_supply("ADP1", "Mains", "0");
  add_supply("BAT0", "Battery");
  EXPECT_FALSE(ac_online(path()));
}

TEST_F(PowerSupplyTreeTest, AnyOnlineMainsAdapterWins) {
  add_supply("AC0", "Mains", "0");
  add_supply("AC1", "Mains", "1");
  EXPECT_TRUE(ac_online(path()));
}

TEST_F(PowerSupplyTreeTest, NonMainsSuppliesDoNotCountAsAdapters) {
  add_supply("ucsi-source-psy-1", "USB", "0");  // a USB-C port, not the wall adapter
  EXPECT_TRUE(ac_online(path()));
}

TEST_F(PowerSupplyTreeTest, MainsWithUnreadableOnlineCountsAsOffline) {
  add_supply("ADP1", "Mains");  // no "online" file
  EXPECT_FALSE(ac_online(path()));
}

// describe_battery(): the wording for the bar tooltip and the Power page.
namespace {
BatteryReading reading(int percent, bool charging, double hours) {
  BatteryReading r;
  r.available = true;
  r.percent = percent;
  r.charging = charging;
  r.hours_remaining = hours;
  return r;
}
}  // namespace

TEST(DescribeBattery, NoBatteryOnMainsJustSaysOnAcPower) {
  const BatteryText t = describe_battery(BatteryReading{}, true);
  EXPECT_EQ(t.headline, "On AC power");
  EXPECT_EQ(t.detail, "");
}

TEST(DescribeBattery, NoBatteryAndNoMains) {
  EXPECT_EQ(describe_battery(BatteryReading{}, false).headline, "No battery");
}

TEST(DescribeBattery, DischargingShowsTimeRemaining) {
  const BatteryText t = describe_battery(reading(63, false, 2.25), false);
  EXPECT_EQ(t.headline, "63% - on battery");
  EXPECT_EQ(t.detail, "2h 15m remaining");
}

TEST(DescribeBattery, ChargingShowsTimeUntilFull) {
  const BatteryText t = describe_battery(reading(87, true, 1.0), true);
  EXPECT_EQ(t.headline, "87% - charging");
  EXPECT_EQ(t.detail, "1h 0m until full");
}

TEST(DescribeBattery, MinutesAreRoundedAndPadded) {
  EXPECT_EQ(describe_battery(reading(50, false, 0.5), false).detail, "0h 30m remaining");
  EXPECT_EQ(describe_battery(reading(50, false, 1.0 + 59.6 / 60), false).detail, "2h 0m remaining");
}

TEST(DescribeBattery, UnknownTimeSaysCalculating) {
  EXPECT_EQ(describe_battery(reading(40, false, -1.0), false).detail, "Time remaining: calculating...");
  EXPECT_EQ(describe_battery(reading(40, true, -1.0), true).detail, "Time until full: calculating...");
}

TEST(DescribeBattery, FullOnMains) {
  const BatteryText t = describe_battery(reading(100, false, 0.0), true);
  EXPECT_EQ(t.headline, "100% - fully charged");
  EXPECT_NE(t.detail.find("full"), std::string::npos);
}

TEST(DescribeBattery, PluggedInButNotChargingIsNotDischarging) {
  const BatteryText t = describe_battery(reading(80, false, -1.0), true);
  EXPECT_EQ(t.headline, "80% - plugged in, not charging");
  EXPECT_EQ(t.detail, "Time left: not discharging");
}

}  // namespace
}  // namespace fleetwm

namespace fleetwm {
namespace {

BatteryReading reading(int percent, bool charging) {
  BatteryReading r;
  r.available = true;
  r.percent = percent;
  r.charging = charging;
  return r;
}

TEST(BatteryFill, RedUnderTenPercentOtherwiseNormal) {
  EXPECT_EQ(battery_fill(reading(9, false), 0, 6).color, BatteryFillColor::Red);
  EXPECT_EQ(battery_fill(reading(0, true), 0, 6).color, BatteryFillColor::Red);
  EXPECT_EQ(battery_fill(reading(10, false), 0, 6).color, BatteryFillColor::Normal);
  EXPECT_EQ(battery_fill(reading(55, true), 3, 6).color, BatteryFillColor::Normal);
}

TEST(BatteryFill, GreenOnlyWhenFull) {
  EXPECT_EQ(battery_fill(reading(100, false), 0, 6).color, BatteryFillColor::Green);
  EXPECT_EQ(battery_fill(reading(100, true), 4, 6).color, BatteryFillColor::Green);
  EXPECT_EQ(battery_fill(reading(99, true), 0, 6).color, BatteryFillColor::Normal);
}

TEST(BatteryFill, ChargingSweepsFromTheRealLevelToTheRightEdge) {
  EXPECT_DOUBLE_EQ(battery_fill(reading(40, true), 0, 6).fraction, 0.40);
  EXPECT_DOUBLE_EQ(battery_fill(reading(40, true), 3, 6).fraction, 0.70);
  EXPECT_DOUBLE_EQ(battery_fill(reading(40, true), 6, 6).fraction, 1.0);
  double last = -1;
  for (int p = 0; p <= 6; ++p) {
    const double f = battery_fill(reading(40, true), p, 6).fraction;
    EXPECT_GT(f, last);
    last = f;
  }
}

TEST(BatteryFill, NotChargingOrFullDoesNotAnimate) {
  for (int p = 0; p <= 6; ++p) {
    EXPECT_DOUBLE_EQ(battery_fill(reading(40, false), p, 6).fraction, 0.40);
    EXPECT_DOUBLE_EQ(battery_fill(reading(100, true), p, 6).fraction, 1.0);
  }
  EXPECT_DOUBLE_EQ(battery_fill(reading(40, true), 99, 6).fraction, 1.0);  // phase is clamped
  EXPECT_DOUBLE_EQ(battery_fill(reading(150, false), 0, 6).fraction, 1.0);
}

}  // namespace
}  // namespace fleetwm
