#include "battery_source.hpp"

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
  BatterySource::Reading reading = battery_internal::read_battery_reading("");
  EXPECT_FALSE(reading.available);
  EXPECT_EQ(reading.percent, 0);
  EXPECT_FALSE(reading.charging);
  EXPECT_EQ(reading.hours_remaining, -1.0);
}

TEST_F(BatterySourceReadingTest, MissingCapacityFileMeansUnavailable) {
  // Directory exists but has none of the expected files -- e.g. the
  // battery vanished mid-poll (hot-unplug) after find_battery_dir() ran.
  BatterySource::Reading reading = battery_internal::read_battery_reading(path());
  EXPECT_FALSE(reading.available);
}

TEST_F(BatterySourceReadingTest, DiscoveringPlainCapacityAndStatus) {
  write("capacity", "73");
  write("status", "Discharging");
  BatterySource::Reading reading = battery_internal::read_battery_reading(path());
  EXPECT_TRUE(reading.available);
  EXPECT_EQ(reading.percent, 73);
  EXPECT_FALSE(reading.charging);
}

TEST_F(BatterySourceReadingTest, ChargingStatusDetected) {
  write("capacity", "50");
  write("status", "Charging");
  BatterySource::Reading reading = battery_internal::read_battery_reading(path());
  EXPECT_TRUE(reading.charging);
}

TEST_F(BatterySourceReadingTest, MissingStatusFileMeansNotCharging) {
  write("capacity", "50");
  BatterySource::Reading reading = battery_internal::read_battery_reading(path());
  EXPECT_TRUE(reading.available);
  EXPECT_FALSE(reading.charging);
}

TEST_F(BatterySourceReadingTest, FullStatusReportsZeroHoursRemaining) {
  write("capacity", "100");
  write("status", "Full");
  BatterySource::Reading reading = battery_internal::read_battery_reading(path());
  EXPECT_DOUBLE_EQ(reading.hours_remaining, 0.0);
}

TEST_F(BatterySourceReadingTest, NoRateFilesMeansHoursRemainingUnknown) {
  write("capacity", "40");
  write("status", "Discharging");
  BatterySource::Reading reading = battery_internal::read_battery_reading(path());
  EXPECT_EQ(reading.hours_remaining, -1.0);
}

TEST_F(BatterySourceReadingTest, ZeroRateMeansHoursRemainingUnknown) {
  write("capacity", "40");
  write("status", "Discharging");
  write("energy_now", "1000");
  write("power_now", "0");
  BatterySource::Reading reading = battery_internal::read_battery_reading(path());
  EXPECT_EQ(reading.hours_remaining, -1.0);
}

TEST_F(BatterySourceReadingTest, DischargingEnergyBasedRateMath) {
  write("capacity", "40");
  write("status", "Discharging");
  write("energy_now", "2000");
  write("power_now", "1000");
  BatterySource::Reading reading = battery_internal::read_battery_reading(path());
  EXPECT_DOUBLE_EQ(reading.hours_remaining, 2.0);
}

TEST_F(BatterySourceReadingTest, ChargingEnergyBasedRateMathNeedsFullValue) {
  write("capacity", "40");
  write("status", "Charging");
  write("energy_now", "2000");
  write("energy_full", "10000");
  write("power_now", "4000");
  BatterySource::Reading reading = battery_internal::read_battery_reading(path());
  EXPECT_DOUBLE_EQ(reading.hours_remaining, 2.0);
}

TEST_F(BatterySourceReadingTest, ChargingWithoutFullValueMeansHoursUnknown) {
  // energy_full/charge_full missing (some drivers don't expose it) --
  // can't compute time-to-full without it.
  write("capacity", "40");
  write("status", "Charging");
  write("energy_now", "2000");
  write("power_now", "4000");
  BatterySource::Reading reading = battery_internal::read_battery_reading(path());
  EXPECT_EQ(reading.hours_remaining, -1.0);
}

TEST_F(BatterySourceReadingTest, ChargeBasedFallbackWhenEnergyFilesAbsent) {
  // Some drivers only expose charge_*/current_* (µAh/µA), not
  // energy_*/power_* (µWh/µW) -- same ratio math applies either way.
  write("capacity", "40");
  write("status", "Discharging");
  write("charge_now", "3000");
  write("current_now", "1500");
  BatterySource::Reading reading = battery_internal::read_battery_reading(path());
  EXPECT_DOUBLE_EQ(reading.hours_remaining, 2.0);
}

TEST_F(BatterySourceReadingTest, EnergyBasedPreferredOverChargeBasedWhenBothPresent) {
  write("capacity", "40");
  write("status", "Discharging");
  write("energy_now", "4000");
  write("power_now", "1000");
  write("charge_now", "999999");
  write("current_now", "1");
  BatterySource::Reading reading = battery_internal::read_battery_reading(path());
  EXPECT_DOUBLE_EQ(reading.hours_remaining, 4.0);
}

TEST_F(BatterySourceReadingTest, CorruptCapacityFileMeansUnavailable) {
  write("capacity", "not-a-number");
  BatterySource::Reading reading = battery_internal::read_battery_reading(path());
  EXPECT_FALSE(reading.available);
}

TEST_F(BatterySourceReadingTest, BatteryPresentFalseWithNoRealSysfsDir) {
  // battery_present() probes the real /sys/class/power_supply, which the
  // test environment (VM/CI) has no BATn entries under -- documents the
  // expected "no battery" desktop/VM behavior rather than asserting a
  // specific value that would vary on real laptop hardware.
  EXPECT_FALSE(BatterySource::battery_present());
}

}  // namespace
}  // namespace fleetwm
