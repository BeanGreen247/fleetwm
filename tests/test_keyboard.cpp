#include <gtest/gtest.h>

#include "keyboard_config.hpp"
#include "test_util.hpp"
#include "xkb_rules.hpp"

using namespace fleetwm;

using KeyboardConfigTest = testutil::ScopedConfigHome;

TEST_F(KeyboardConfigTest, DefaultsToOneUsLayoutWithBothSwitchKeys) {
  const KeyboardConfig c = load_keyboard_config();
  ASSERT_EQ(c.layouts.size(), 1u);
  EXPECT_EQ(c.layouts[0], (KeyboardLayout{"us", ""}));
  EXPECT_EQ(c.switch_keys, LayoutSwitchKeys::Both);
  EXPECT_EQ(c.repeat_rate, 25);
  EXPECT_TRUE(c.locales.empty());
}

TEST_F(KeyboardConfigTest, RoundTrips) {
  KeyboardConfig c;
  c.layouts = {{"us", ""}, {"cz", "qwerty"}, {"de", "nodeadkeys"}};
  c.options = "caps:escape";
  c.switch_keys = LayoutSwitchKeys::AltShift;
  c.repeat_rate = 40;
  c.repeat_delay = 300;
  c.locales = {"cs_CZ.UTF-8", "de_DE.UTF-8"};
  save_keyboard_config(c);
  EXPECT_EQ(load_keyboard_config(), c);
}

TEST_F(KeyboardConfigTest, BadValuesAreDroppedNotTrusted) {
  write_config("keyboard.toml",
                       "layouts = [\"us\", \"bad name\", \"cz:qwerty\", \"us\", \"../x\"]\n"
                       "options = \"caps:escape;rm -rf\"\nrepeat_rate = 5000\nlocales = [\"cs_CZ.UTF-8\", \"x y\"]\n");
  const KeyboardConfig c = load_keyboard_config();
  ASSERT_EQ(c.layouts.size(), 2u);
  EXPECT_EQ(c.layouts[1], (KeyboardLayout{"cz", "qwerty"}));
  EXPECT_TRUE(c.options.empty());
  EXPECT_EQ(c.repeat_rate, 100);
  ASSERT_EQ(c.locales.size(), 1u);
}

TEST_F(KeyboardConfigTest, EmptyOrBrokenFileGivesAUsableKeyboard) {
  write_config("keyboard.toml", "layouts = []\n");
  EXPECT_EQ(load_keyboard_config().layouts.size(), 1u);
  write_config("keyboard.toml", "this is [not toml");
  EXPECT_EQ(load_keyboard_config(), KeyboardConfig{});
}

TEST(KeyboardNames, XkbArgumentsAreCommaJoinedInOrder) {
  KeyboardConfig c;
  c.layouts = {{"us", ""}, {"cz", "qwerty"}, {"de", ""}};
  const XkbNames n = xkb_names_for(c);
  EXPECT_EQ(n.layout, "us,cz,de");
  EXPECT_EQ(n.variant, ",qwerty,");
}

TEST(KeyboardNames, SpecsAndPillText) {
  KeyboardLayout l;
  EXPECT_TRUE(parse_layout_spec("cz:qwerty", &l));
  EXPECT_EQ(l, (KeyboardLayout{"cz", "qwerty"}));
  EXPECT_EQ(layout_spec(l), "cz:qwerty");
  EXPECT_FALSE(parse_layout_spec("", &l));
  EXPECT_FALSE(parse_layout_spec("us;x", &l));
  EXPECT_EQ(layout_pill_text({"cz", "qwerty"}), "CZ");
  EXPECT_EQ(layout_pill_text({"ara", ""}), "ARA");
}

TEST(XkbRules, ParsesLayoutsWithTheirVariantsRightAfter) {
  const auto all = parse_xkb_rules(
      "! model\n  pc105           Generic 105-key PC\n\n"
      "! layout\n  us              English (US)\n  cz              Czech\n  de              German\n\n"
      "! variant\n  intl            us: English (US, intl., with dead keys)\n"
      "  qwerty          cz: Czech (QWERTY)\n  nodeadkeys      de: German (no dead keys)\n"
      "  weird           zz: Orphan\n\n! option\n  grp             Switching\n");
  ASSERT_EQ(all.size(), 6u);  // 3 layouts + 3 variants; the orphan variant of an unknown layout is dropped
  EXPECT_EQ(all[0].layout, "us");
  EXPECT_EQ(all[1].variant, "intl");
  EXPECT_EQ(all[1].description, "English (US, intl., with dead keys)");
  EXPECT_EQ(all[2].layout, "cz");
  EXPECT_EQ(describe_layout(all, "cz", "qwerty"), "Czech (QWERTY)");
  EXPECT_EQ(describe_layout(all, "fr", ""), "fr");
  EXPECT_EQ(describe_layout(all, "fr", "oss"), "fr (oss)");
}

TEST(Locales, OnlyUtf8LocalesSortedAndUnique) {
  const auto l = parse_supported_locales("cs_CZ ISO-8859-2\ncs_CZ.UTF-8 UTF-8\naa_DJ.UTF-8 UTF-8\naa_DJ.UTF-8 UTF-8\n");
  ASSERT_EQ(l.size(), 2u);
  EXPECT_EQ(l[0].code, "aa_DJ.UTF-8");
  EXPECT_EQ(l[1].code, "cs_CZ.UTF-8");
}

TEST(Locales, DisplayNameFromTheDefinitionHeader) {
  EXPECT_EQ(locale_display_name("LC_IDENTIFICATION\ntitle \"x\"\nlanguage \"Czech\"\nterritory \"Czechia\"\n"), "Czech (Czechia)");
  EXPECT_EQ(locale_display_name("language \"Esperanto\"\n"), "Esperanto");
  EXPECT_EQ(locale_display_name("nothing here"), "");
}
