#include <gtest/gtest.h>

#include <algorithm>

#include "shortcut_list.hpp"

using namespace fleetwm;

namespace {
const ShortcutEntry* find(const std::vector<ShortcutEntry>& list, const std::string& description) {
  for (const ShortcutEntry& e : list)
    if (e.description.find(description) != std::string::npos) return &e;
  return nullptr;
}
}  // namespace

TEST(FormatAltCombo, LowercaseLetterIsPlainAlt) {
  EXPECT_EQ(format_alt_combo("d"), "Alt+D");
  EXPECT_EQ(format_alt_combo("h"), "Alt+H");
}

TEST(FormatAltCombo, UppercaseLetterMeansShift) {
  EXPECT_EQ(format_alt_combo("Q"), "Alt+Shift+Q");
  EXPECT_EQ(format_alt_combo("P"), "Alt+Shift+P");
}

TEST(FormatAltCombo, CommonKeyNamesAreFriendly) {
  EXPECT_EQ(format_alt_combo("Return"), "Alt+Enter");
  EXPECT_EQ(format_alt_combo("Escape"), "Alt+Esc");
  EXPECT_EQ(format_alt_combo("space"), "Alt+Space");
  EXPECT_EQ(format_alt_combo("slash"), "Alt+/");
  EXPECT_EQ(format_alt_combo("comma"), "Alt+,");
}

TEST(FormatAltCombo, QuestionNeedsShift) {
  EXPECT_EQ(format_alt_combo("question"), "Alt+Shift+/");
}

TEST(FormatAltCombo, UnknownNamesPassThrough) {
  EXPECT_EQ(format_alt_combo("F5"), "Alt+F5");
  EXPECT_EQ(format_alt_combo("Tab"), "Alt+Tab");
}

TEST(FormatAltCombo, EmptyIsUnbound) {
  EXPECT_EQ(format_alt_combo(""), "(unbound)");
}

TEST(FormatAltShiftCombo, AlwaysIncludesShift) {
  EXPECT_EQ(format_alt_shift_combo("Return"), "Alt+Shift+Enter");
  EXPECT_EQ(format_alt_shift_combo("d"), "Alt+Shift+D");
  EXPECT_EQ(format_alt_shift_combo("Q"), "Alt+Shift+Q");
}

TEST(ShortcutList, DefaultsListTheExpectedKeys) {
  const auto list = build_shortcut_list(KeybindsConfig{}, WindowLayout::Tiling);
  EXPECT_EQ(find(list, "Open a terminal")->keys, "Alt+Enter");
  EXPECT_EQ(find(list, "Application launcher")->keys, "Alt+D");
  EXPECT_EQ(find(list, "Close the focused window")->keys, "Alt+Shift+Q");
  EXPECT_EQ(find(list, "Pin the focused window")->keys, "Alt+Shift+P");
  EXPECT_EQ(find(list, "list of shortcuts")->keys, "Alt+Shift+/");
  EXPECT_EQ(find(list, "master")->keys, "Alt+Shift+Enter");
}

TEST(ShortcutList, RemappedKeysShowUp) {
  KeybindsConfig b;
  b.launcher = "space";
  b.close_window = "w";
  b.shortcuts = "F1";
  const auto list = build_shortcut_list(b, WindowLayout::Tiling);
  EXPECT_EQ(find(list, "Application launcher")->keys, "Alt+Space");
  EXPECT_EQ(find(list, "Close the focused window")->keys, "Alt+W");
  EXPECT_EQ(find(list, "list of shortcuts")->keys, "Alt+F1");
}

TEST(ShortcutList, TilingHasEverythingActive) {
  const auto list = build_shortcut_list(KeybindsConfig{}, WindowLayout::Tiling);
  for (const ShortcutEntry& e : list) EXPECT_TRUE(e.active) << e.description;
}

TEST(ShortcutList, DesktopOnlyKeepsTerminalAndShortcutsActive) {
  const auto list = build_shortcut_list(KeybindsConfig{}, WindowLayout::Desktop);
  EXPECT_TRUE(find(list, "Open a terminal")->active);
  EXPECT_TRUE(find(list, "list of shortcuts")->active);
  for (const char* off : {"Application launcher", "Close the focused window", "Lock the screen",
                          "Pin the focused window", "Focus the window to the left", "Quit fleetwm"})
    EXPECT_FALSE(find(list, off)->active) << off;
}

TEST(ShortcutList, DesktopAddsMouseAndTaskbarGestures) {
  const auto list = build_shortcut_list(KeybindsConfig{}, WindowLayout::Desktop);
  EXPECT_NE(find(list, "Move the window"), nullptr);
  EXPECT_NE(find(list, "Resize the window"), nullptr);
  EXPECT_NE(find(list, "Snap"), nullptr);
  EXPECT_NE(find(list, "Open the start menu"), nullptr);
  EXPECT_EQ(find(list, "Focus follows the mouse"), nullptr);
}

TEST(ShortcutList, TilingMouseNotesDifferFromDesktop) {
  const auto list = build_shortcut_list(KeybindsConfig{}, WindowLayout::Tiling);
  EXPECT_NE(find(list, "Focus follows the mouse"), nullptr);
  EXPECT_EQ(find(list, "Move the window"), nullptr);
}

TEST(ShortcutList, EveryEntryHasASectionKeysAndDescription) {
  for (WindowLayout l : {WindowLayout::Tiling, WindowLayout::Desktop})
    for (const ShortcutEntry& e : build_shortcut_list(KeybindsConfig{}, l)) {
      EXPECT_FALSE(e.section.empty());
      EXPECT_FALSE(e.keys.empty());
      EXPECT_FALSE(e.description.empty());
    }
}

TEST(ShortcutList, DocumentationLinksAreHttps) {
  EXPECT_EQ(std::string(kDocsUrl).rfind("https://", 0), 0u);
  EXPECT_EQ(std::string(kShortcutsDocUrl).rfind("https://", 0), 0u);
}
