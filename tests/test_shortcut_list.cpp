#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>

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
  EXPECT_EQ(format_alt_combo("question"), "Alt+Shift+/");  // still understood if someone binds it
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
  EXPECT_EQ(find(list, "list of shortcuts")->keys, "Super+/");
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
  EXPECT_EQ(find(list, "list of shortcuts")->keys, "Super+F1");
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

TEST(ShortcutList, DesktopListsTheAppShortcutsAndSuper) {
  const auto list = build_shortcut_list(KeybindsConfig{}, WindowLayout::Desktop);
  EXPECT_EQ(find(list, "web browser")->keys, "Super+Shift+B");
  EXPECT_EQ(find(list, "file manager")->keys, "Super+Shift+E");
  EXPECT_EQ(find(list, "text editor")->keys, "Super+Shift+T");
  EXPECT_NE(find(list, "start menu"), nullptr);
  for (const char* d : {"web browser", "file manager", "text editor", "start menu"})
    EXPECT_TRUE(find(list, d)->active) << d;
}

TEST(ShortcutList, TilingDoesNotListTheDesktopAppShortcuts) {
  const auto list = build_shortcut_list(KeybindsConfig{}, WindowLayout::Tiling);
  EXPECT_EQ(find(list, "web browser"), nullptr);
  EXPECT_EQ(find(list, "file manager"), nullptr);
  EXPECT_EQ(find(list, "text editor"), nullptr);
  EXPECT_EQ(find(list, "start menu"), nullptr);
}

TEST(ShortcutList, RemappedAppShortcutsShowUp) {
  KeybindsConfig b;
  b.browser = "w";
  b.file_manager = "F";
  const auto list = build_shortcut_list(b, WindowLayout::Desktop);
  EXPECT_EQ(find(list, "web browser")->keys, "Super+Shift+W");
  EXPECT_EQ(find(list, "file manager")->keys, "Super+Shift+F");
}

TEST(FormatSuperCombo, UsesTheSuperPrefix) {
  EXPECT_EQ(format_super_combo("b"), "Super+B");
  EXPECT_EQ(format_super_combo("slash"), "Super+/");
  EXPECT_EQ(format_super_combo("Q"), "Super+Shift+Q");
  EXPECT_EQ(format_super_combo("Return"), "Super+Enter");
  EXPECT_EQ(format_super_combo(""), "(unbound)");
}

// The Super binds exist so they never shadow what runs inside a terminal: tmux's
// Alt bindings and readline's. None of the defaults may be an Alt combination that
// either of them uses.
TEST(ShortcutList, DefaultsAvoidTmuxAndReadlineAltBindings) {
  const KeybindsConfig b;
  for (const std::string& key : {b.browser, b.file_manager, b.text_editor, b.shortcuts}) {
    const std::string combo = format_super_combo(key);
    EXPECT_EQ(combo.rfind("Super+", 0), 0u) << combo;
  }
  // tmux's default Alt bindings: 1-7 (layouts), n/p (windows with alerts), o (rotate),
  // plus arrow resizing; readline's common ones: b f d t u l c ?
  const char* risky[] = {"1", "2", "3", "4", "5", "6", "7", "n", "p", "o", "b", "f", "d", "t", "u", "l", "c", "?"};
  for (const char* k : risky) {
    for (const ShortcutEntry& e : build_shortcut_list(b, WindowLayout::Desktop)) {
      if (!e.active) continue;
      EXPECT_NE(e.keys, std::string("Alt+") + static_cast<char>(std::toupper(k[0])))
          << "an active Desktop shortcut shadows Alt+" << k << " (" << e.description << ")";
    }
  }
}

TEST(FormatModCombo, PrettyPrintsTheConfiguredModifier) {
  EXPECT_EQ(format_mod_combo("super", "b"), "Super+B");
  EXPECT_EQ(format_mod_combo("logo", "b"), "Super+B");
  EXPECT_EQ(format_mod_combo("win", "b"), "Super+B");
  EXPECT_EQ(format_mod_combo("ctrl+alt", "t"), "Ctrl+Alt+T");
  EXPECT_EQ(format_mod_combo("Control+Alt", "t"), "Ctrl+Alt+T");
  EXPECT_EQ(format_mod_combo("alt", "b", true), "Alt+Shift+B");
}

TEST(FormatModCombo, EmptyOrBlankModifierFallsBackToSuper) {
  EXPECT_EQ(format_mod_combo("", "b"), "Super+B");
  EXPECT_EQ(format_mod_combo("+", "b"), "Super+B");
}

TEST(ShortcutList, AppShortcutsAlwaysShowShift) {
  const auto list = build_shortcut_list(KeybindsConfig{}, WindowLayout::Desktop);
  for (const char* d : {"web browser", "file manager", "text editor"})
    EXPECT_NE(find(list, d)->keys.find("Shift+"), std::string::npos) << d;
}

TEST(ShortcutList, RemappedModifierShowsInTheLabels) {
  KeybindsConfig b;
  b.modifier = "ctrl+alt";
  const auto list = build_shortcut_list(b, WindowLayout::Desktop);
  EXPECT_EQ(find(list, "web browser")->keys, "Ctrl+Alt+Shift+B");
  EXPECT_EQ(find(list, "list of shortcuts")->keys, "Ctrl+Alt+/");
}

TEST(ShortcutList, StartMenuKeyLabelFollowsTheConfig) {
  KeybindsConfig b;
  EXPECT_EQ(find(build_shortcut_list(b, WindowLayout::Desktop), "start menu")->keys, "Super (tap)");
  b.start_menu_key = "Menu";
  EXPECT_EQ(find(build_shortcut_list(b, WindowLayout::Desktop), "start menu")->keys, "Menu (tap)");
  b.start_menu_key = "F12, Super_L";
  EXPECT_EQ(find(build_shortcut_list(b, WindowLayout::Desktop), "start menu")->keys, "F12 (tap)");
}
