#include "autostart.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "test_util.hpp"

namespace fleetwm {
namespace {

using AutostartTest = testutil::ScopedConfigHome;

AutostartApp app(const std::string& id = "demo.desktop") {
  AutostartApp a;
  a.id = id;
  a.name = "Demo";
  a.exec = "demo --flag %U";
  a.icon = "demo";
  return a;
}

std::string slurp(const std::string& p) {
  std::ifstream f(p);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

TEST_F(AutostartTest, NothingIsEnabledToStartWith) {
  EXPECT_TRUE(autostart_enabled_ids().empty());
  EXPECT_TRUE(autostart_commands().empty());
}

TEST_F(AutostartTest, TurnOnWritesAMarkedXdgEntryAndOffDeletesIt) {
  autostart_set(app(), true);
  const std::string path = autostart_dir() + "/demo.desktop";
  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_NE(slurp(path).find("X-Fleetwm-Autostart=true"), std::string::npos);
  EXPECT_EQ(autostart_enabled_ids(), std::set<std::string>{"demo.desktop"});
  EXPECT_EQ(autostart_commands(), std::vector<std::string>{"demo --flag"});
  autostart_set(app(), false);
  EXPECT_FALSE(std::filesystem::exists(path));
  EXPECT_TRUE(autostart_enabled_ids().empty());
}

TEST_F(AutostartTest, AFileFromElsewhereIsHiddenNotDeletedAndRestoredOnTurnOn) {
  std::filesystem::create_directories(autostart_dir());
  const std::string path = autostart_dir() + "/other.desktop";
  { std::ofstream(path) << "[Desktop Entry]\nType=Application\nName=Other\nExec=other\n\n[Desktop Action x]\nName=X\nExec=x\n"; }
  EXPECT_EQ(autostart_commands(), std::vector<std::string>{"other"});
  autostart_set(app("other.desktop"), false);
  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_TRUE(autostart_enabled_ids().empty());
  EXPECT_NE(slurp(path).find("[Desktop Action x]"), std::string::npos);
  autostart_set(app("other.desktop"), true);
  EXPECT_EQ(slurp(path).find("Hidden"), std::string::npos);
  EXPECT_EQ(autostart_commands(), std::vector<std::string>{"other"});
}

TEST_F(AutostartTest, GnomeDisabledEntriesAreOff) {
  std::filesystem::create_directories(autostart_dir());
  { std::ofstream(autostart_dir() + "/g.desktop") << "[Desktop Entry]\nType=Application\nName=G\nExec=g\nX-GNOME-Autostart-enabled=false\n"; }
  EXPECT_TRUE(autostart_enabled_ids().empty());
}

TEST(AutostartCommand, DropsFieldCodesKeepsPercent) {
  EXPECT_EQ(autostart_command("app %U"), "app");
  EXPECT_EQ(autostart_command("app --size=100%% %f"), "app --size=100%");
  EXPECT_EQ(autostart_command("sh -c 'echo hi'"), "sh -c 'echo hi'");
}

TEST(AutostartCommand, FileTextCarriesTheMarker) {
  const std::string t = autostart_file_text(app());
  EXPECT_NE(t.find("Exec=demo --flag %U"), std::string::npos);
  EXPECT_NE(t.find("Icon=demo"), std::string::npos);
  EXPECT_NE(t.find("Terminal=false"), std::string::npos);
}

}  // namespace
}  // namespace fleetwm
