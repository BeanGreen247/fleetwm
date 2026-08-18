#include "keybinds_config.hpp"

#include <gtest/gtest.h>

#include <fstream>

#include "test_util.hpp"

namespace fleetwm {
namespace {

using KeybindsConfigTest = testutil::ScopedConfigHome;

TEST_F(KeybindsConfigTest, LoadWithNoConfigFileReturnsDefaults) {
  KeybindsConfig config = load_keybinds_config();
  EXPECT_EQ(config.terminal, "Return");
  EXPECT_EQ(config.launcher, "d");
  EXPECT_EQ(config.close_window, "Q");
  EXPECT_EQ(config.toggle_pin, "P");
  EXPECT_EQ(config.toggle_float, "F");
  EXPECT_EQ(config.lock, "L");
  EXPECT_EQ(config.screenshot, "S");
  EXPECT_EQ(config.focus_left, "h");
  EXPECT_EQ(config.focus_down, "j");
  EXPECT_EQ(config.focus_up, "k");
  EXPECT_EQ(config.focus_right, "l");
  EXPECT_EQ(config.quit, "Escape");
  EXPECT_EQ(config.debug_overlay, "I");
}

TEST_F(KeybindsConfigTest, SaveThenLoadRoundTripsEveryField) {
  KeybindsConfig config;
  config.terminal = "KP_Enter";
  config.launcher = "space";
  config.close_window = "W";
  config.toggle_pin = "T";
  config.toggle_float = "G";
  config.lock = "X";
  config.screenshot = "Print";
  config.focus_left = "Left";
  config.focus_down = "Down";
  config.focus_up = "Up";
  config.focus_right = "Right";
  config.quit = "q";
  config.debug_overlay = "O";

  save_keybinds_config(config);
  KeybindsConfig loaded = load_keybinds_config();

  EXPECT_EQ(loaded.terminal, "KP_Enter");
  EXPECT_EQ(loaded.launcher, "space");
  EXPECT_EQ(loaded.close_window, "W");
  EXPECT_EQ(loaded.toggle_pin, "T");
  EXPECT_EQ(loaded.toggle_float, "G");
  EXPECT_EQ(loaded.lock, "X");
  EXPECT_EQ(loaded.screenshot, "Print");
  EXPECT_EQ(loaded.focus_left, "Left");
  EXPECT_EQ(loaded.focus_down, "Down");
  EXPECT_EQ(loaded.focus_up, "Up");
  EXPECT_EQ(loaded.focus_right, "Right");
  EXPECT_EQ(loaded.quit, "q");
  EXPECT_EQ(loaded.debug_overlay, "O");
}

TEST_F(KeybindsConfigTest, PartialConfigKeepsDefaultsForMissingKeys) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "launcher = \"space\"\nquit = \"q\"\n";
  out.close();

  KeybindsConfig config = load_keybinds_config();
  EXPECT_EQ(config.launcher, "space");
  EXPECT_EQ(config.quit, "q");
  // Untouched fields stay at their KeybindsConfig{} defaults.
  EXPECT_EQ(config.terminal, "Return");
  EXPECT_EQ(config.focus_left, "h");
}

// Two keys that intentionally share the same underlying bind, only
// distinguished by Shift at the caller (input.cpp) -- config storage
// itself has no notion of that relationship, so both must be able to
// hold the same value simultaneously without one clobbering the other.
TEST_F(KeybindsConfigTest, TerminalAndCloseWindowCanShareTheSameKeyName) {
  KeybindsConfig config;
  config.terminal = "Q";
  config.close_window = "Q";
  save_keybinds_config(config);

  KeybindsConfig loaded = load_keybinds_config();
  EXPECT_EQ(loaded.terminal, "Q");
  EXPECT_EQ(loaded.close_window, "Q");
}

}  // namespace
}  // namespace fleetwm
