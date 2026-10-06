// The profile-guided build trains the instrumented desktop on a virtual screen before the final compile
// (scripts/pgo-train-session.sh, started by scripts/build-pgo-auto.sh). What it does not run is code the
// final binaries are not optimized for, so these tests keep the training long enough and wide enough:
// every desktop program, every Settings page, both layouts, glass on and off, dark and light. They read
// the scripts as text; nothing is started.
#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace tr_fs = std::filesystem;

std::string tr_read(const tr_fs::path& p) {
  std::ifstream in(p);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

tr_fs::path tr_root() { return tr_fs::path(FLEETWM_SOURCE_DIR); }

std::string tr_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

}  // namespace

TEST(PgoTraining, ScriptsAreValidShell) {
  for (const char* script : {"scripts/pgo-train-session.sh", "scripts/build-pgo-auto.sh", "scripts/build-pgo.sh", "install.sh"}) {
    const std::string cmd = "bash -n '" + (tr_root() / script).string() + "' 2>&1";
    EXPECT_EQ(std::system(cmd.c_str()), 0) << script << " has a shell syntax error";
  }
}

TEST(PgoTraining, RunsLongEnoughToCoverEverything) {
  const std::string driver = tr_read(tr_root() / "scripts/build-pgo-auto.sh");
  std::smatch m;
  ASSERT_TRUE(std::regex_search(driver, m, std::regex(R"(TRAIN_SECONDS="\$\{1:-(\d+)\}")"))) << "default training length not found";
  EXPECT_GE(std::stoi(m[1]), 120) << "a short training run leaves most of the desktop unprofiled";
}

TEST(PgoTraining, StartsEveryDesktopProgramThatCanRunOnAVirtualScreen) {
  const std::string script = tr_read(tr_root() / "scripts/pgo-train-session.sh");
  for (const char* program : {"src/bar/fleetwm-bar", "src/wallpaper/fleetwm-wallpaper", "apps/lockapplet/fleetwm-lockapplet",
                              "apps/audiomixer/fleetwm-audiomixer", "apps/settings/fleetwm-settings", "apps/launcher/fleetwm-launcher",
                              "apps/powermenu/fleetwm-powermenu", "apps/shortcuts/fleetwm-shortcuts", "apps/langpicker/fleetwm-langpicker"}) {
    EXPECT_NE(script.find(program), std::string::npos) << program << " is never started by the training run";
    const std::string dir = std::string(program).substr(0, std::string(program).rfind('/'));
    EXPECT_TRUE(tr_fs::is_directory(tr_root() / dir)) << dir << ": the training script names a program that no longer exists";
  }
}

TEST(PgoTraining, VisitsEverySettingsPage) {
  const std::string script = tr_read(tr_root() / "scripts/pgo-train-session.sh");
  std::smatch m;
  ASSERT_TRUE(std::regex_search(script, m, std::regex(R"(SETTINGS_PAGES=\(([^)]*)\))"))) << "no SETTINGS_PAGES list";
  std::set<std::string> pages;
  std::stringstream ss(m[1]);
  for (std::string p; ss >> p;) pages.insert(p);
  // The tab names are in apps/settings/main.cpp; a page argument matches the start of a tab name, case-insensitively.
  const std::string settings = tr_read(tr_root() / "apps/settings/main.cpp");
  std::smatch names;
  ASSERT_TRUE(std::regex_search(settings, names, std::regex(R"(tab_names\{([^}]*)\})")));
  std::vector<std::string> tabs;
  const std::string tab_list = names[1];
  const std::regex quoted("\"([^\"]+)\"");
  for (std::sregex_iterator it(tab_list.begin(), tab_list.end(), quoted), end; it != end; ++it) tabs.push_back((*it)[1]);
  ASSERT_GE(tabs.size(), 10u);
  for (const std::string& tab : tabs) {
    bool visited = false;
    for (const std::string& page : pages) visited = visited || tr_lower(tab).rfind(page, 0) == 0;
    EXPECT_TRUE(visited) << "Settings tab '" << tab << "' is never opened by the training run";
  }
  for (const std::string& page : pages) {
    bool real = false;
    for (const std::string& tab : tabs) real = real || tr_lower(tab).rfind(page, 0) == 0;
    EXPECT_TRUE(real) << "'" << page << "' matches no Settings tab";
  }
}

TEST(PgoTraining, CoversBothLayoutsGlassOnAndOffAndBothColourModes) {
  const std::string script = tr_read(tr_root() / "scripts/pgo-train-session.sh");
  std::smatch m;
  ASSERT_TRUE(std::regex_search(script, m, std::regex(R"(COMBOS=\(([^)]*)\))"))) << "no COMBOS list";
  std::set<std::string> layouts, glass, modes;
  const std::string combo_list = m[1];
  const std::regex combo("\"(\\w+) (\\w+) (\\w+)\"");
  for (std::sregex_iterator it(combo_list.begin(), combo_list.end(), combo), end; it != end; ++it) {
    layouts.insert((*it)[1]);
    glass.insert((*it)[2]);
    modes.insert((*it)[3]);
  }
  EXPECT_EQ(layouts, (std::set<std::string>{"desktop", "tiling"}));
  EXPECT_EQ(glass, (std::set<std::string>{"false", "true"}));
  EXPECT_EQ(modes, (std::set<std::string>{"dark", "light"}));
}

TEST(PgoTraining, ExercisesTheInteractionPathsAndSkipsMissingToolsInsteadOfFailing) {
  const std::string script = tr_read(tr_root() / "scripts/pgo-train-session.sh");
  // What the run drives: the things a real session spends its time in.
  for (const char* what : {"alt_tab", "start_menu", "snap ", "overlay_desktop", "overlay_tiling", "pointer_sweep", "next_layout",
                           "WORKSPACE ", "IDLE_INHIBIT", "WINDOW_ACTIVATE", "WINDOW_CLOSE", "LAYOUT_NEXT", "OUTPUTS?", "grim", "foot"})
    EXPECT_NE(script.find(what), std::string::npos) << "the training run no longer exercises: " << what;
  // Optional tools are guarded, and one failing step must not end the whole run.
  for (const char* tool : {"wtype", "wlrctl", "grim", "foot"})
    EXPECT_NE(script.find(std::string("have ") + tool), std::string::npos) << tool << " must be checked for before use";
  const std::string head = script.substr(0, script.find("TRAIN_SECONDS="));
  EXPECT_NE(head.find("set -uo pipefail"), std::string::npos);
  EXPECT_EQ(script.find("set -e\n"), std::string::npos) << "set -e would abort the profile at the first harmless failure";
  EXPECT_NE(script.find("time_left"), std::string::npos) << "the run must be bounded by TRAIN_SECONDS";
}

TEST(PgoTraining, TheInstallerTellsTheTruthAboutTheLength) {
  const std::string install = tr_read(tr_root() / "install.sh");
  EXPECT_EQ(install.find("short training"), std::string::npos) << "the training run is no longer short; say what it does";
  EXPECT_NE(install.find("training"), std::string::npos);
}
