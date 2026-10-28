#include <gtest/gtest.h>

#include "launcher_match.hpp"

namespace fleetwm {
namespace {

TEST(LauncherMatch, RecognisesLaunchersStoresAndSettingsWindows) {
  for (const char* t : {"Battle.net", "Rockstar Games Launcher", "The Witcher 2 Launcher", "Loria Configuration", "Heroic Games Launcher", "Epic Games Launcher",
                        "C:\\ProgramData\\Battle.net\\Agent\\Agent.9852\\Agent.exe", "GOG GALAXY", "winecfg", "Wine Installer"})
    EXPECT_TRUE(looks_like_launcher_text(t)) << t;
}

TEST(LauncherMatch, LeavesGamesAlone) {
  for (const char* t : {"World of Warcraft", "Grand Theft Auto III - The Definitive Edition", "POSTAL 2", "Half-Life 2 - OpenGL", "osu!", "LibertyCity.exe", "Rayman Origins"})
    EXPECT_FALSE(looks_like_launcher_text(t)) << t;
}

}  // namespace
}  // namespace fleetwm
