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

namespace fleetwm {
namespace {
std::string temp_gate_dir() {
  char tmpl[] = "/tmp/fleetwm-gate-XXXXXX";
  return std::string(mkdtemp(tmpl));
}
const std::vector<SteamGame> kGames = {{"10", "A"}, {"20", "B"}, {"30", "C"}};

TEST(AllGamesGpu, ReadFromTheSwitches) {
  EXPECT_EQ(all_games_gpu_from_switches({true, true}), AllGamesGpu::Dedicated);
  EXPECT_EQ(all_games_gpu_from_switches({false, false}), AllGamesGpu::Integrated);
  EXPECT_EQ(all_games_gpu_from_switches({true, false}), AllGamesGpu::None);
  EXPECT_EQ(all_games_gpu_from_switches({}), AllGamesGpu::None);
}

TEST(AllGamesGpu, ApplyWritesEveryGame) {
  const std::string dir = temp_gate_dir();
  ASSERT_TRUE(apply_all_games_gpu(kGames, AllGamesGpu::Dedicated, dir));
  for (const SteamGame& g : kGames) EXPECT_TRUE(game_switch_get(g.appid, GameSwitch::DedicatedGpu, dir));
  ASSERT_TRUE(apply_all_games_gpu(kGames, AllGamesGpu::Integrated, dir));
  for (const SteamGame& g : kGames) EXPECT_FALSE(game_switch_get(g.appid, GameSwitch::DedicatedGpu, dir));
  apply_all_games_gpu(kGames, AllGamesGpu::Dedicated, dir);
  ASSERT_TRUE(apply_all_games_gpu(kGames, AllGamesGpu::None, dir));  // None changes nothing
  EXPECT_TRUE(game_switch_get("10", GameSwitch::DedicatedGpu, dir));
}

TEST(AllGamesGpu, SettingsChangeIsWrittenToTuxThrottle) {
  const std::string dir = temp_gate_dir();
  GameConfig c;
  c.all_games_gpu = AllGamesGpu::Dedicated;
  c.all_games_gpu_stamp = 4000000000LL;  // changed here later than any file time
  EXPECT_FALSE(sync_all_games_gpu(&c, kGames, 4000000001LL, dir));
  for (const SteamGame& g : kGames) EXPECT_TRUE(game_switch_get(g.appid, GameSwitch::DedicatedGpu, dir));
  EXPECT_EQ(c.all_games_gpu, AllGamesGpu::Dedicated);
}

TEST(AllGamesGpu, TuxThrottleChangeIsReadIntoTheSettings) {
  const std::string dir = temp_gate_dir();
  GameConfig c;
  c.all_games_gpu = AllGamesGpu::Dedicated;
  c.all_games_gpu_stamp = 1000;  // long ago: the files are newer
  apply_all_games_gpu(kGames, AllGamesGpu::Integrated, dir);
  game_switch_set("20", GameSwitch::DedicatedGpu, true, dir);  // one switched on in TuxThrottle
  EXPECT_TRUE(sync_all_games_gpu(&c, kGames, 2000000000LL, dir));
  EXPECT_EQ(c.all_games_gpu, AllGamesGpu::None);  // a mix
  EXPECT_TRUE(game_switch_get("20", GameSwitch::DedicatedGpu, dir));  // and nothing was overwritten
  EXPECT_FALSE(game_switch_get("10", GameSwitch::DedicatedGpu, dir));
}

TEST(AllGamesGpu, NothingChosenTakesTuxThrottlesState) {
  const std::string dir = temp_gate_dir();
  apply_all_games_gpu(kGames, AllGamesGpu::Dedicated, dir);
  GameConfig c;
  EXPECT_TRUE(sync_all_games_gpu(&c, kGames, 2000000000LL, dir));
  EXPECT_EQ(c.all_games_gpu, AllGamesGpu::Dedicated);
  EXPECT_FALSE(sync_all_games_gpu(&c, kGames, 2000000005LL, dir));  // settled
}

TEST(AllGamesGpu, NewGameFollowsTheChoice) {
  const std::string dir = temp_gate_dir();
  GameConfig c;
  c.all_games_gpu = AllGamesGpu::Dedicated;
  c.all_games_gpu_stamp = 4000000000LL;
  sync_all_games_gpu(&c, kGames, 4000000001LL, dir);
  std::vector<SteamGame> more = kGames;
  more.push_back({"40", "D"});
  EXPECT_FALSE(sync_all_games_gpu(&c, more, 4000000002LL, dir));
  EXPECT_TRUE(game_switch_get("40", GameSwitch::DedicatedGpu, dir));
}
}  // namespace
}  // namespace fleetwm
