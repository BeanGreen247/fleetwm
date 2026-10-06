#pragma once

#include <string>
#include <vector>

// Keyboard settings (Settings -> Keyboard), saved per user in keyboard.toml: the layouts you
// can switch between, how to switch, key repeat, and the languages (locales) you picked.

namespace fleetwm {

struct KeyboardLayout {
  std::string layout;   // xkb layout name: "us", "cz", "de"
  std::string variant;  // xkb variant: "", "qwerty", "intl"
  bool operator==(const KeyboardLayout&) const = default;
};

enum class LayoutSwitchKeys { SuperSpace, AltShift, Both };

struct KeyboardConfig {
  std::vector<KeyboardLayout> layouts{{"us", ""}};  // never empty; the first one is the starting layout
  std::string model;                                  // xkb model, empty = default
  std::string options;                                // extra xkb options, e.g. "caps:escape"
  LayoutSwitchKeys switch_keys = LayoutSwitchKeys::Both;
  int repeat_rate = 25;    // keys per second
  int repeat_delay = 600;  // ms before repeating starts
  std::vector<std::string> locales;  // "cs_CZ.UTF-8": languages the user wants available
  bool operator==(const KeyboardConfig&) const = default;
};

std::string keyboard_user_config_path();

// Loads keyboard.toml; a missing or broken file gives the defaults (one US layout).
KeyboardConfig load_keyboard_config();
// Throws std::runtime_error on I/O failure.
void save_keyboard_config(const KeyboardConfig& config);

// "us" or "cz:qwerty" -> layout + variant; "" when it is not a valid name.
bool parse_layout_spec(const std::string& spec, KeyboardLayout* out);
std::string layout_spec(const KeyboardLayout& l);

// The three arguments xkb wants, comma-joined in order: layout "us,cz", variant ",qwerty".
struct XkbNames {
  std::string layout, variant, model, options;
};
XkbNames xkb_names_for(const KeyboardConfig& config);

// Short text for the bar pill: "US", "CZ".
std::string layout_pill_text(const KeyboardLayout& l);

const char* switch_keys_name(LayoutSwitchKeys k);
LayoutSwitchKeys switch_keys_from_name(const std::string& s);

}  // namespace fleetwm
