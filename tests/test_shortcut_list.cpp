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

TEST(FormatKeyCombo, PrettyPrintsModifiersInConventionalOrder) {
  EXPECT_EQ(format_key_combo("ctrl+alt+t"), "Ctrl+Alt+T");
  EXPECT_EQ(format_key_combo("alt+ctrl+t"), "Ctrl+Alt+T");  // written in any order
  EXPECT_EQ(format_key_combo("super+shift+b"), "Super+Shift+B");
  EXPECT_EQ(format_key_combo("shift+super+b"), "Super+Shift+B");
  EXPECT_EQ(format_key_combo("ctrl+alt+shift+super+x"), "Super+Ctrl+Alt+Shift+X");
}

TEST(FormatKeyCombo, ModifierSynonyms) {
  EXPECT_EQ(format_key_combo("logo+e"), "Super+E");
  EXPECT_EQ(format_key_combo("win+e"), "Super+E");
  EXPECT_EQ(format_key_combo("meta+e"), "Super+E");
  EXPECT_EQ(format_key_combo("Control+Alt+Delete"), "Ctrl+Alt+Delete");
}

TEST(FormatKeyCombo, KeyNamesAreFriendly) {
  EXPECT_EQ(format_key_combo("super+slash"), "Super+/");
  EXPECT_EQ(format_key_combo("alt+Return"), "Alt+Enter");
  EXPECT_EQ(format_key_combo("ctrl+space"), "Ctrl+Space");
  EXPECT_EQ(format_key_combo("F12"), "F12");  // a bare key is a valid combo
}

TEST(FormatKeyCombo, UnparseableIsUnbound) {
  EXPECT_EQ(format_key_combo(""), "(unbound)");
  EXPECT_EQ(format_key_combo("ctrl+alt+"), "(unbound)");
  EXPECT_EQ(format_key_combo("hyper+t"), "(unbound)");
}

TEST(ShortcutList, TilingDefaultsListTheExpectedKeys) {
  const auto list = build_shortcut_list(KeybindsConfig{}, WindowLayout::Tiling);
  EXPECT_EQ(find(list, "Open a terminal")->keys, "Alt+Enter");
  EXPECT_EQ(find(list, "Application launcher")->keys, "Alt+D");
  EXPECT_EQ(find(list, "Close the focused window")->keys, "Alt+Shift+Q");
  EXPECT_EQ(find(list, "Pin the focused window")->keys, "Alt+Shift+P");
  EXPECT_EQ(find(list, "list of shortcuts")->keys, "Super+/");
  EXPECT_EQ(find(list, "master")->keys, "Alt+Shift+Enter");
}

TEST(ShortcutList, DesktopTerminalHasItsOwnKeysNotAltEnter) {
  const auto list = build_shortcut_list(KeybindsConfig{}, WindowLayout::Desktop);
  const ShortcutEntry* term = find(list, "Open a terminal");
  ASSERT_NE(term, nullptr);
  EXPECT_EQ(term->keys, "Ctrl+Alt+T");
  EXPECT_TRUE(term->active);
  for (const ShortcutEntry& e : list)
    EXPECT_NE(e.keys, "Alt+Enter") << "Alt+Enter must not be offered in the Desktop layout";
}

TEST(ShortcutList, WindowsTaskManagerShortcutIsDesktopOnly) {
  const auto desktop = build_shortcut_list(KeybindsConfig{}, WindowLayout::Desktop);
  const ShortcutEntry* taskmgr = find(desktop, "Task Manager");
  ASSERT_NE(taskmgr, nullptr);
  EXPECT_EQ(taskmgr->keys, "Ctrl+Shift+Esc");
  EXPECT_TRUE(taskmgr->active);
  EXPECT_EQ(find(build_shortcut_list(KeybindsConfig{}, WindowLayout::Tiling), "Task Manager"), nullptr);
}

TEST(ShortcutList, RemappedKeysShowUp) {
  KeybindsConfig b;
  b.launcher = "space";
  b.close_window = "w";
  b.shortcuts_help = "F1";
  b.desktop_terminal = "super+Return";
  const auto tiling = build_shortcut_list(b, WindowLayout::Tiling);
  EXPECT_EQ(find(tiling, "Application launcher")->keys, "Alt+Space");
  EXPECT_EQ(find(tiling, "Close the focused window")->keys, "Alt+W");
  EXPECT_EQ(find(tiling, "list of shortcuts")->keys, "F1");
  EXPECT_EQ(find(build_shortcut_list(b, WindowLayout::Desktop), "Open a terminal")->keys, "Super+Enter");
}

TEST(ShortcutList, TilingHasEverythingActive) {
  const auto list = build_shortcut_list(KeybindsConfig{}, WindowLayout::Tiling);
  for (const ShortcutEntry& e : list) EXPECT_TRUE(e.active) << e.description;
}

TEST(ShortcutList, DesktopKeepsOnlyTheDesktopCombosActive) {
  const auto list = build_shortcut_list(KeybindsConfig{}, WindowLayout::Desktop);
  for (const char* on : {"Open a terminal", "list of shortcuts", "web browser", "file manager",
                         "text editor", "start menu"})
    EXPECT_TRUE(find(list, on)->active) << on;
  for (const char* off : {"Application launcher", "Close the focused window", "Lock the screen",
                          "Pin the focused window", "Focus the window to the left", "Quit fleetwm"})
    EXPECT_FALSE(find(list, off)->active) << off;
}

TEST(ShortcutList, DesktopAppShortcutsUseTheirOwnCombos) {
  const auto list = build_shortcut_list(KeybindsConfig{}, WindowLayout::Desktop);
  EXPECT_EQ(find(list, "web browser")->keys, "Super+Shift+B");
  EXPECT_EQ(find(list, "file manager")->keys, "Super+Shift+E");
  EXPECT_EQ(find(list, "text editor")->keys, "Super+Shift+T");
  for (const char* d : {"web browser", "file manager", "text editor"})
    EXPECT_NE(find(list, d)->keys.find("Shift+"), std::string::npos) << d << " must require Shift";
}

TEST(ShortcutList, TilingDoesNotListTheDesktopOnlyEntries) {
  const auto list = build_shortcut_list(KeybindsConfig{}, WindowLayout::Tiling);
  for (const char* d : {"web browser", "file manager", "text editor", "start menu", "Move the window"})
    EXPECT_EQ(find(list, d), nullptr) << d;
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

TEST(ShortcutList, StartMenuKeyLabelFollowsTheConfig) {
  KeybindsConfig b;
  EXPECT_EQ(find(build_shortcut_list(b, WindowLayout::Desktop), "start menu")->keys, "Super (tap)");
  b.start_menu_key = "Menu";
  EXPECT_EQ(find(build_shortcut_list(b, WindowLayout::Desktop), "start menu")->keys, "Menu (tap)");
  b.start_menu_key = "F12, Super_L";
  EXPECT_EQ(find(build_shortcut_list(b, WindowLayout::Desktop), "start menu")->keys, "F12 (tap)");
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

// The Desktop defaults must not shadow what runs inside a terminal: tmux's Alt
// bindings (1-7, n, p, o) and readline's (b f d t u l c ?). tmux's prefix is Ctrl+b.
TEST(ShortcutList, DesktopDefaultsAvoidTmuxAndReadline) {
  const auto list = build_shortcut_list(KeybindsConfig{}, WindowLayout::Desktop);
  const char* risky[] = {"1", "2", "3", "4", "5", "6", "7", "n", "p", "o", "b", "f", "d", "t", "u", "l", "c", "?"};
  for (const ShortcutEntry& e : list) {
    if (!e.active) continue;
    for (const char* k : risky)
      EXPECT_NE(e.keys, std::string("Alt+") + static_cast<char>(std::toupper(k[0])))
          << e.description << " shadows Alt+" << k;
    EXPECT_NE(e.keys, "Ctrl+B") << e.description << " shadows tmux's prefix";
  }
}

TEST(ShortcutList, DebugOverlayHasADesktopCombo) {
  const auto desktop = build_shortcut_list(KeybindsConfig{}, WindowLayout::Desktop);
  EXPECT_EQ(find(desktop, "overlay")->keys, "Ctrl+Alt+I");
  EXPECT_TRUE(find(desktop, "overlay")->active);
  const auto tiling = build_shortcut_list(KeybindsConfig{}, WindowLayout::Tiling);
  EXPECT_EQ(find(tiling, "overlay")->keys, "Alt+Shift+I");
}

TEST(ShortcutList, WindowSwitchingShortcutsWorkInBothLayouts) {
  for (WindowLayout l : {WindowLayout::Tiling, WindowLayout::Desktop}) {
    const auto list = build_shortcut_list(KeybindsConfig{}, l);
    EXPECT_EQ(find(list, "windows on this workspace (")->keys, "Alt+Tab");
    EXPECT_EQ(find(list, "workspace, the other way")->keys, "Alt+Shift+Tab");
    EXPECT_EQ(find(list, "Go to workspace")->keys, "Super+1 to 0");
    EXPECT_EQ(find(list, "Send the window to workspace")->keys, "Super+Shift+1 to 0");
    EXPECT_EQ(find(list, "Previous workspace")->keys, "Ctrl+Alt+Left");
    EXPECT_EQ(find(list, "Next workspace")->keys, "Ctrl+Alt+Right");
    EXPECT_EQ(find(list, "previous screen")->keys, "Super+Shift+Left");
    EXPECT_EQ(find(list, "next screen")->keys, "Super+Shift+Right");
    for (const char* d : {"windows on this workspace (", "Go to workspace", "previous screen"})
      EXPECT_TRUE(find(list, d)->active) << d;
  }
}

TEST(ShortcutList, SnapKeysAreListedOnlyInTheDesktopLayout) {
  const auto desktop = build_shortcut_list(KeybindsConfig{}, WindowLayout::Desktop);
  EXPECT_EQ(find(desktop, "Snap left")->keys, "Super+Left");
  EXPECT_EQ(find(desktop, "Snap right")->keys, "Super+Right");
  EXPECT_EQ(find(desktop, "Top half, then maximize")->keys, "Super+Up");
  EXPECT_EQ(find(desktop, "Restore from maximized")->keys, "Super+Down");
  EXPECT_EQ(find(build_shortcut_list(KeybindsConfig{}, WindowLayout::Tiling), "Snap left"), nullptr);
}

TEST(ShortcutList, RemappedWorkspaceModifiersShowUp) {
  KeybindsConfig b;
  b.workspace_switch = "ctrl";
  b.workspace_send = "ctrl+shift";
  const auto list = build_shortcut_list(b, WindowLayout::Tiling);
  EXPECT_EQ(find(list, "Go to workspace")->keys, "Ctrl+1 to 0");
  EXPECT_EQ(find(list, "Send the window to workspace")->keys, "Ctrl+Shift+1 to 0");
}

TEST(ShortcutList, DesktopHasWindowsStyleWindowKeys) {
  const auto list = build_shortcut_list(KeybindsConfig{}, WindowLayout::Desktop);
  EXPECT_EQ(find(list, "Show the desktop")->keys, "Super+D");
  EXPECT_EQ(find(list, "Minimize all")->keys, "Super+M");
  EXPECT_EQ(find(list, "Restore all")->keys, "Super+Shift+M");
  EXPECT_EQ(find(list, "Maximize or restore the focused")->keys, "Alt+F10");
  for (const char* d : {"Show the desktop", "Minimize all", "Restore all", "Maximize or restore the focused"})
    EXPECT_TRUE(find(list, d)->active) << d;
}

TEST(ShortcutList, DesktopCloseWindowIsAltF4) {
  const auto list = build_shortcut_list(KeybindsConfig{}, WindowLayout::Desktop);
  bool seen = false;
  for (const ShortcutEntry& e : list)
    if (e.keys == "Alt+F4") {
      seen = true;
      EXPECT_TRUE(e.active);
    }
  EXPECT_TRUE(seen);
}

TEST(ShortcutList, DesktopOnlyWindowKeysAreAbsentInTiling) {
  const auto list = build_shortcut_list(KeybindsConfig{}, WindowLayout::Tiling);
  EXPECT_EQ(find(list, "Show the desktop"), nullptr);
  EXPECT_EQ(find(list, "Minimize all"), nullptr);
  for (const ShortcutEntry& e : list) EXPECT_NE(e.keys, "Alt+F4");
}
