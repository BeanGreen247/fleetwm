#include <gtest/gtest.h>

#include <unistd.h>

#include <filesystem>

#include "steam_games.hpp"

namespace fleetwm {
namespace {

TEST(SteamGames, ParsesAManifest) {
  const std::string acf = "\"AppState\"\n{\n\t\"appid\"\t\t\"22100\"\n\t\"Universe\"\t\t\"1\"\n\t\"name\"\t\t\"Mount and Blade\"\n\t\"StateFlags\"\t\t\"4\"\n}\n";
  const SteamGame g = parse_steam_manifest(acf);
  EXPECT_EQ(g.appid, "22100");
  EXPECT_EQ(g.name, "Mount and Blade");
}

TEST(SteamGames, RejectsAManifestWithoutAValidId) {
  EXPECT_TRUE(parse_steam_manifest("\"AppState\" { \"name\" \"X\" }").appid.empty());
  EXPECT_TRUE(parse_steam_manifest("\"appid\" \"../etc\" \"name\" \"X\"").appid.empty());
  EXPECT_TRUE(parse_steam_manifest("").name.empty());
}

TEST(SteamGames, ListsLibraryFolders) {
  const std::string vdf = "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"/home/u/.local/share/Steam\"\n\t\t\"label\"\t\t\"\"\n\t}\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"/mnt/Games/SteamLibrary\"\n\t}\n}\n";
  const auto paths = parse_steam_library_paths(vdf);
  ASSERT_EQ(paths.size(), 2u);
  EXPECT_EQ(paths[0], "/home/u/.local/share/Steam");
  EXPECT_EQ(paths[1], "/mnt/Games/SteamLibrary");
  EXPECT_TRUE(parse_steam_library_paths("").empty());
}

TEST(SteamGames, ToolsAreNotGames) {
  EXPECT_TRUE(is_steam_tool("Proton 9.0 (Beta)"));
  EXPECT_TRUE(is_steam_tool("Proton - Experimental"));
  EXPECT_TRUE(is_steam_tool("Steam Linux Runtime 3.0 (sniper)"));
  EXPECT_TRUE(is_steam_tool("Steamworks Common Redistributables"));
  EXPECT_FALSE(is_steam_tool("Mount and Blade"));
  EXPECT_FALSE(is_steam_tool("Protonmail Game"));
}

TEST(SteamGames, SwitchesAreFilesNamedAfterTheAppId) {
  const std::string dir = (std::filesystem::temp_directory_path() / ("fleetwm-steam-switch-" + std::to_string(::getpid()))).string();
  EXPECT_FALSE(game_switch_get("22100", GameSwitch::MangoHud, dir));
  EXPECT_TRUE(game_switch_set("22100", GameSwitch::MangoHud, true, dir));
  EXPECT_TRUE(game_switch_get("22100", GameSwitch::MangoHud, dir));
  EXPECT_TRUE(std::filesystem::exists(dir + "/22100.on"));
  EXPECT_FALSE(game_switch_get("22100", GameSwitch::DedicatedGpu, dir));
  EXPECT_TRUE(game_switch_set("22100", GameSwitch::DedicatedGpu, true, dir));
  EXPECT_TRUE(std::filesystem::exists(dir + "/22100.dgpu"));
  EXPECT_TRUE(game_switch_set("22100", GameSwitch::MangoHud, false, dir));
  EXPECT_FALSE(game_switch_get("22100", GameSwitch::MangoHud, dir));
  EXPECT_TRUE(game_switch_set("22100", GameSwitch::MangoHud, false, dir));  // already off is fine
  EXPECT_FALSE(game_switch_set("../x", GameSwitch::MangoHud, true, dir));   // never a path outside the folder
  std::filesystem::remove_all(dir);
}

}  // namespace
}  // namespace fleetwm
