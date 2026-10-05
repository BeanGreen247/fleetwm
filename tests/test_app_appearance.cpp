#include <gtest/gtest.h>

#include "app_appearance.hpp"

using namespace fleetwm;

TEST(ThemeIsDark, OnlyLightIsLight) {
  EXPECT_FALSE(theme_is_dark(ThemeName::Light));
  EXPECT_TRUE(theme_is_dark(ThemeName::Dark));
  EXPECT_TRUE(theme_is_dark(ThemeName::Catppuccin));
  EXPECT_TRUE(theme_is_dark(ThemeName::Dracula));
  EXPECT_TRUE(theme_is_dark(ThemeName::OledBlack));
}

TEST(MergeGtkSettings, CreatesTheFileContentFromNothing) {
  EXPECT_EQ(merge_gtk_settings("", true), "[Settings]\ngtk-application-prefer-dark-theme=true\n");
  EXPECT_EQ(merge_gtk_settings("", false), "[Settings]\ngtk-application-prefer-dark-theme=false\n");
}

TEST(MergeGtkSettings, ChangesTheKeyInPlace) {
  const std::string in = "[Settings]\ngtk-font-name=Sans 10\ngtk-application-prefer-dark-theme=false\ngtk-icon-theme-name=Adwaita\n";
  EXPECT_EQ(merge_gtk_settings(in, true),
            "[Settings]\ngtk-font-name=Sans 10\ngtk-application-prefer-dark-theme=true\ngtk-icon-theme-name=Adwaita\n");
}

TEST(MergeGtkSettings, KeepsOtherSettingsWhenAddingTheKey) {
  const std::string in = "[Settings]\ngtk-font-name=Sans 10\n";
  EXPECT_EQ(merge_gtk_settings(in, true), "[Settings]\ngtk-font-name=Sans 10\ngtk-application-prefer-dark-theme=true\n");
}

TEST(MergeGtkSettings, AddsAfterTheSettingsSectionNotAnotherOne) {
  const std::string in = "[Settings]\ngtk-font-name=Sans 10\n\n[Other]\nx=1\n";
  const std::string out = merge_gtk_settings(in, true);
  EXPECT_EQ(out, "[Settings]\ngtk-font-name=Sans 10\ngtk-application-prefer-dark-theme=true\n\n[Other]\nx=1\n");
}

TEST(MergeGtkSettings, IgnoresTheKeyInOtherSections) {
  const std::string in = "[Other]\ngtk-application-prefer-dark-theme=false\n";
  const std::string out = merge_gtk_settings(in, true);
  EXPECT_NE(out.find("[Other]\ngtk-application-prefer-dark-theme=false"), std::string::npos);
  EXPECT_NE(out.find("[Settings]\ngtk-application-prefer-dark-theme=true"), std::string::npos);
}

TEST(MergeGtkSettings, AppendsASettingsSectionWhenOnlyOthersExist) {
  EXPECT_EQ(merge_gtk_settings("[Other]\nx=1\n", false), "[Other]\nx=1\n\n[Settings]\ngtk-application-prefer-dark-theme=false\n");
}

TEST(MergeGtkSettings, IsStableWhenAppliedTwice) {
  const std::string once = merge_gtk_settings("[Settings]\na=b\n", true);
  EXPECT_EQ(merge_gtk_settings(once, true), once);
}

TEST(MergeGtkSettings, FlipsBackAndForth) {
  const std::string dark = merge_gtk_settings("[Settings]\na=b\n", true);
  const std::string light = merge_gtk_settings(dark, false);
  EXPECT_NE(light.find("=false"), std::string::npos);
  EXPECT_EQ(light.find("=true"), std::string::npos);
  EXPECT_NE(light.find("a=b"), std::string::npos);
}

TEST(AppearanceScript, SetsTheColourSchemeForTheTheme) {
  EXPECT_NE(appearance_shell_script(true).find("color-scheme 'prefer-dark'"), std::string::npos);
  EXPECT_NE(appearance_shell_script(false).find("color-scheme 'prefer-light'"), std::string::npos);
}

TEST(AppearanceScript, OnlySwitchesAPlainAdwaitaTheme) {
  const std::string s = appearance_shell_script(true);
  EXPECT_NE(s.find("gtk-theme 'Adwaita-dark'"), std::string::npos);
  EXPECT_NE(s.find("case \"$cur\""), std::string::npos);  // guarded by the current theme
  EXPECT_NE(appearance_shell_script(false).find("gtk-theme 'Adwaita'"), std::string::npos);
}

TEST(AppearanceScript, DoesNothingWithoutGsettings) {
  EXPECT_EQ(appearance_shell_script(true).rfind("command -v gsettings", 0), 0u);
}
