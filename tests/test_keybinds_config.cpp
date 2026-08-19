#include "keybinds_config.hpp"

#include <gtest/gtest.h>
#include <toml++/toml.h>

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

// -- individual field round trips (only one key set per test) --------------
// Each field maps to its own distinct TOML key -- a typo in any one
// mapping (load or save side) would only show up as a failure in that
// field's own test, not get masked by the others changing alongside it in
// SaveThenLoadRoundTripsEveryField above.

TEST_F(KeybindsConfigTest, OnlyTerminalSetKeepsOtherDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "terminal = \"KP_Enter\"\n";
  out.close();
  KeybindsConfig config = load_keybinds_config();
  EXPECT_EQ(config.terminal, "KP_Enter");
  EXPECT_EQ(config.launcher, "d");
}

TEST_F(KeybindsConfigTest, OnlyLauncherSetKeepsOtherDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "launcher = \"space\"\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().launcher, "space");
}

TEST_F(KeybindsConfigTest, OnlyCloseWindowSetKeepsOtherDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "close_window = \"W\"\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().close_window, "W");
}

TEST_F(KeybindsConfigTest, OnlyTogglePinSetKeepsOtherDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "toggle_pin = \"T\"\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().toggle_pin, "T");
}

TEST_F(KeybindsConfigTest, OnlyToggleFloatSetKeepsOtherDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "toggle_float = \"G\"\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().toggle_float, "G");
}

TEST_F(KeybindsConfigTest, OnlyLockSetKeepsOtherDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "lock = \"X\"\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().lock, "X");
}

TEST_F(KeybindsConfigTest, OnlyScreenshotSetKeepsOtherDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "screenshot = \"Print\"\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().screenshot, "Print");
}

TEST_F(KeybindsConfigTest, OnlyFocusLeftSetKeepsOtherDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "focus_left = \"Left\"\n";
  out.close();
  KeybindsConfig config = load_keybinds_config();
  EXPECT_EQ(config.focus_left, "Left");
  EXPECT_EQ(config.focus_down, "j");
  EXPECT_EQ(config.focus_up, "k");
  EXPECT_EQ(config.focus_right, "l");
}

TEST_F(KeybindsConfigTest, OnlyFocusDownSetKeepsOtherDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "focus_down = \"Down\"\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().focus_down, "Down");
}

TEST_F(KeybindsConfigTest, OnlyFocusUpSetKeepsOtherDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "focus_up = \"Up\"\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().focus_up, "Up");
}

TEST_F(KeybindsConfigTest, OnlyFocusRightSetKeepsOtherDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "focus_right = \"Right\"\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().focus_right, "Right");
}

TEST_F(KeybindsConfigTest, OnlyQuitSetKeepsOtherDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "quit = \"q\"\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().quit, "q");
}

TEST_F(KeybindsConfigTest, OnlyDebugOverlaySetKeepsOtherDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "debug_overlay = \"O\"\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().debug_overlay, "O");
}

// -- per-field wrong-type-is-ignored -----------------------------------------

TEST_F(KeybindsConfigTest, WrongTypeTerminalIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "terminal = 1\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().terminal, "Return");
}

TEST_F(KeybindsConfigTest, WrongTypeLauncherIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "launcher = true\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().launcher, "d");
}

TEST_F(KeybindsConfigTest, WrongTypeCloseWindowIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "close_window = 3.14\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().close_window, "Q");
}

TEST_F(KeybindsConfigTest, WrongTypeFocusLeftIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "focus_left = [1, 2]\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().focus_left, "h");
}

TEST_F(KeybindsConfigTest, WrongTypeDebugOverlayIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "debug_overlay = false\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().debug_overlay, "I");
}

TEST_F(KeybindsConfigTest, WrongTypeTogglePinIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "toggle_pin = 1\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().toggle_pin, "P");
}

TEST_F(KeybindsConfigTest, WrongTypeToggleFloatIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "toggle_float = true\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().toggle_float, "F");
}

TEST_F(KeybindsConfigTest, WrongTypeLockIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "lock = 9\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().lock, "L");
}

TEST_F(KeybindsConfigTest, WrongTypeScreenshotIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "screenshot = false\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().screenshot, "S");
}

TEST_F(KeybindsConfigTest, WrongTypeFocusDownIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "focus_down = 2.5\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().focus_down, "j");
}

TEST_F(KeybindsConfigTest, WrongTypeFocusUpIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "focus_up = [1]\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().focus_up, "k");
}

TEST_F(KeybindsConfigTest, WrongTypeFocusRightIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "focus_right = 7\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().focus_right, "l");
}

TEST_F(KeybindsConfigTest, WrongTypeQuitIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "quit = true\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().quit, "Escape");
}

// -- misc --------------------------------------------------------------------

TEST_F(KeybindsConfigTest, MalformedTomlThrows) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "terminal = \"unterminated\n";
  out.close();
  EXPECT_THROW(load_keybinds_config(), toml::parse_error);
}

TEST_F(KeybindsConfigTest, EmptyConfigFileYieldsDefaults) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream(dir_ / "fleetwm" / "keybinds.toml").close();
  EXPECT_EQ(load_keybinds_config().terminal, "Return");
}

TEST_F(KeybindsConfigTest, UnrelatedKeyIsIgnoredNotFatal) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "some_future_bind = \"Z\"\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().terminal, "Return");
}

TEST_F(KeybindsConfigTest, EveryFieldCanIndependentlyDifferFromItsDefault) {
  // Reassigning every field to a value none of the others default to,
  // confirming load doesn't cross-wire any two keys under refactor
  // (each field asserted individually rather than in one block, so a
  // future field reordering that swaps two assignments fails on the
  // specific field, not just "something in this test broke").
  KeybindsConfig config;
  config.terminal = "1";
  config.launcher = "2";
  config.close_window = "3";
  config.toggle_pin = "4";
  config.toggle_float = "5";
  config.lock = "6";
  config.screenshot = "7";
  config.focus_left = "8";
  config.focus_down = "9";
  config.focus_up = "0";
  config.focus_right = "F1";
  config.quit = "F2";
  config.debug_overlay = "F3";
  save_keybinds_config(config);

  KeybindsConfig loaded = load_keybinds_config();
  EXPECT_EQ(loaded.terminal, "1");
  EXPECT_EQ(loaded.launcher, "2");
  EXPECT_EQ(loaded.close_window, "3");
  EXPECT_EQ(loaded.toggle_pin, "4");
  EXPECT_EQ(loaded.toggle_float, "5");
  EXPECT_EQ(loaded.lock, "6");
  EXPECT_EQ(loaded.screenshot, "7");
  EXPECT_EQ(loaded.focus_left, "8");
  EXPECT_EQ(loaded.focus_down, "9");
  EXPECT_EQ(loaded.focus_up, "0");
  EXPECT_EQ(loaded.focus_right, "F1");
  EXPECT_EQ(loaded.quit, "F2");
  EXPECT_EQ(loaded.debug_overlay, "F3");
}

TEST_F(KeybindsConfigTest, FunctionKeyNamesRoundTrip) {
  KeybindsConfig config;
  config.screenshot = "F12";
  config.debug_overlay = "F1";
  save_keybinds_config(config);

  KeybindsConfig loaded = load_keybinds_config();
  EXPECT_EQ(loaded.screenshot, "F12");
  EXPECT_EQ(loaded.debug_overlay, "F1");
}

TEST_F(KeybindsConfigTest, DigitKeyNamesRoundTrip) {
  KeybindsConfig config;
  config.launcher = "1";
  config.close_window = "9";
  save_keybinds_config(config);

  KeybindsConfig loaded = load_keybinds_config();
  EXPECT_EQ(loaded.launcher, "1");
  EXPECT_EQ(loaded.close_window, "9");
}

TEST_F(KeybindsConfigTest, Xf86StyleKeysymNameRoundTrips) {
  // xkb keysym names aren't limited to single characters -- multimedia
  // keys like XF86AudioMute are valid xkb_keysym_from_name() input too
  // (see keybinds_config.hpp's doc comment on resolution), so the config
  // layer must pass an arbitrarily long keysym name through unchanged.
  KeybindsConfig config;
  config.lock = "XF86ScreenSaver";
  save_keybinds_config(config);
  EXPECT_EQ(load_keybinds_config().lock, "XF86ScreenSaver");
}

TEST_F(KeybindsConfigTest, KpEnterKeysymNameRoundTrips) {
  KeybindsConfig config;
  config.terminal = "KP_Enter";
  save_keybinds_config(config);
  EXPECT_EQ(load_keybinds_config().terminal, "KP_Enter");
}

TEST_F(KeybindsConfigTest, WrongTypeToggleFloatAsArrayIgnored) {
  std::filesystem::create_directories(dir_ / "fleetwm");
  std::ofstream out(dir_ / "fleetwm" / "keybinds.toml");
  out << "toggle_float = [\"F\"]\n";
  out.close();
  EXPECT_EQ(load_keybinds_config().toggle_float, "F");
}

TEST_F(KeybindsConfigTest, EmptyStringKeyNameRoundTrips) {
  // Not validated against being a real xkb keysym at this layer -- that
  // check (and its "ignore an unresolvable bind" fallback) happens where
  // keybinds_from_config() actually calls xkb_keysym_from_name()
  // (input.cpp), not here. Config load/save is a pure passthrough.
  KeybindsConfig config;
  config.launcher = "";
  save_keybinds_config(config);
  EXPECT_EQ(load_keybinds_config().launcher, "");
}

TEST_F(KeybindsConfigTest, AllTwelveDefaultsAreDistinctSingleOrNamedKeys) {
  // Sanity check on the shipped default set itself: terminal/close_window
  // intentionally alias (see input.cpp's Shift-distinguishes-them
  // design, already covered by TerminalAndCloseWindowCanShareTheSameKeyName
  // above) but every OTHER pair of defaults must be distinct, or two
  // global binds would silently collide out of the box.
  KeybindsConfig config;
  EXPECT_NE(config.launcher, config.toggle_pin);
  EXPECT_NE(config.toggle_pin, config.toggle_float);
  EXPECT_NE(config.lock, config.screenshot);
  EXPECT_NE(config.focus_left, config.focus_right);
  EXPECT_NE(config.focus_up, config.focus_down);
  EXPECT_NE(config.quit, config.debug_overlay);
}

TEST_F(KeybindsConfigTest, UserConfigPathEndsWithKeybindsToml) {
  EXPECT_NE(keybinds_user_config_path().find("keybinds.toml"), std::string::npos);
}

TEST_F(KeybindsConfigTest, SystemDefaultConfigPathDiffersFromUserPath) {
  EXPECT_NE(keybinds_user_config_path(), keybinds_system_default_config_path());
}

}  // namespace
}  // namespace fleetwm
