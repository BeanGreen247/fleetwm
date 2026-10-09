// The profile-guided build trains the instrumented desktop on a virtual screen before the final compile
// (scripts/pgo-train-session.sh, started by scripts/build-pgo-auto.sh). What it does not run is code the
// final binaries are not optimized for, so these tests keep the training long enough and wide enough:
// every desktop program, every Settings page, both layouts, glass on and off, dark and light. They read
// the scripts as text; nothing is started.
#include <gtest/gtest.h>

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
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
  // Measured (docs/OPTIMIZATIONS.md, 2026-10-07): the dense workload covers 99% of a 120 s run's code by 60 s,
  // and six rounds (one per look) need about 85 s. Shorter than that leaves looks, taskbar edges or icon states out.
  EXPECT_GE(std::stoi(m[1]), 70) << "a short training run leaves looks, taskbar edges and icon states unprofiled";
  EXPECT_LE(std::stoi(m[1]), 150) << "the training is a big part of the install time; it was made dense so it can be short";
}

TEST(PgoTraining, StartsEveryDesktopProgramThatCanRunOnAVirtualScreen) {
  const std::string script = tr_read(tr_root() / "scripts/pgo-train-session.sh");
  for (const char* program : {"src/bar/fleetwm-bar", "src/wallpaper/fleetwm-wallpaper", "apps/lockapplet/fleetwm-lockapplet",
                              "apps/audiomixer/fleetwm-audiomixer", "apps/settings/fleetwm-settings", "apps/launcher/fleetwm-launcher",
                              "apps/powermenu/fleetwm-powermenu", "apps/shortcuts/fleetwm-shortcuts", "apps/langpicker/fleetwm-langpicker",
                              "apps/desktop/fleetwm-desktop", "apps/fleetfm/fleetwm-fm", "apps/config/fleetwm-config"}) {
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

TEST(PgoTraining, CoversBothLayoutsGlassOnAndOffAndEveryTheme) {
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
  EXPECT_EQ(modes, (std::set<std::string>{"dark", "light", "catppuccin", "dracula", "oled_black"})) << "every theme must be drawn";
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

// ---- the instrumented programs are the ones the training starts ---------------------------------------
//
// The compositor and the desktop start the bar, the wallpaper, the launcher and the rest by name. On a
// machine that already has Fleetwm installed that finds the old copies, which record nothing for the
// profile (and on a fresh machine it finds nothing). scripts/pgo-path-shim.sh puts the built programs
// first on PATH.

namespace {

std::string tr_run(const std::string& cmd) {
  std::string out;
  if (FILE* p = popen((cmd + " 2>&1").c_str(), "r")) {
    char buf[512];
    while (std::fgets(buf, sizeof buf, p)) out += buf;
    pclose(p);
  }
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  return out;
}

void tr_write_program(const tr_fs::path& path, const std::string& says) {
  tr_fs::create_directories(path.parent_path());
  std::ofstream(path) << "#!/bin/sh\necho " << says << "\n";
  tr_fs::permissions(path, tr_fs::perms::owner_all | tr_fs::perms::group_read | tr_fs::perms::group_exec,
                     tr_fs::perm_options::replace);
}

// A throwaway build tree, an "already installed" decoy directory and a shim directory.
struct TrTree {
  tr_fs::path root, build, installed, shim;
  TrTree() {
    char tmpl[] = "/tmp/fleetwm-shim-test-XXXXXX";
    root = ::mkdtemp(tmpl);
    build = root / "build";
    installed = root / "installed";
    shim = root / "shim";
    tr_write_program(build / "src/bar/fleetwm-bar", "built-bar");
    tr_write_program(build / "src/wallpaper/fleetwm-wallpaper", "built-wallpaper");
    tr_write_program(build / "apps/launcher/fleetwm-launcher", "built-launcher");
    tr_write_program(build / "apps/powermenu/fleetwm-powermenu", "built-powermenu");
    tr_write_program(build / "src/compositor/fleetwm", "built-compositor");
    tr_write_program(build / "tests/fleetwm-unit-tests", "built-tests");
    tr_write_program(build / "src/locker/fleetwm-locker", "built-locker");
    tr_write_program(build / "src/greeter/fleetwm-greet", "built-greet");
    tr_write_program(build / "src/greeter-login/fleetwm-greeter-login", "built-greeter-login");
    std::ofstream(build / "src/bar/fleetwm-notes.txt") << "not a program\n";  // no execute bit
    std::ofstream(build / "src/bar/libfleetwm-bits.a") << "archive\n";
    tr_write_program(installed / "fleetwm-bar", "OLD-installed-bar");
    tr_write_program(installed / "fleetwm-launcher", "OLD-installed-launcher");
    tr_write_program(installed / "fleetwm-settings", "OLD-installed-settings");
  }
  ~TrTree() {
    std::error_code ec;
    tr_fs::remove_all(root, ec);
  }
  std::string shim_cmd() const {
    return "bash '" + (tr_root() / "scripts/pgo-path-shim.sh").string() + "' '" + build.string() + "' '" + shim.string() + "'";
  }
  // What running `name` finds when the shim comes first, then the installed decoys, then the system.
  std::string run_by_name(const std::string& name) const {
    return tr_run("PATH='" + shim.string() + ":" + installed.string() + ":/usr/bin:/bin' " + name);
  }
};

}  // namespace

TEST(PgoTrainingShim, BuiltProgramsWinOverInstalledCopies) {
  TrTree t;
  ASSERT_EQ(tr_run(t.shim_cmd()), "");
  EXPECT_EQ(t.run_by_name("fleetwm-bar"), "built-bar") << "the old installed bar would record nothing";
  EXPECT_EQ(t.run_by_name("fleetwm-launcher"), "built-launcher");
  EXPECT_EQ(t.run_by_name("fleetwm-wallpaper"), "built-wallpaper") << "not installed at all, only built";
  EXPECT_EQ(t.run_by_name("fleetwm-powermenu"), "built-powermenu");
  EXPECT_EQ(t.run_by_name("fleetwm"), "built-compositor");
}

TEST(PgoTrainingShim, ProgramsThatMustNotRunDuringTrainingAreLeftOut) {
  TrTree t;
  ASSERT_EQ(tr_run(t.shim_cmd()), "");
  for (const char* excluded : {"fleetwm-unit-tests", "fleetwm-locker", "fleetwm-greet", "fleetwm-greeter-login", "fleetwm-notes.txt", "libfleetwm-bits.a"})
    EXPECT_FALSE(tr_fs::exists(t.shim / excluded)) << excluded << " must not be on the training PATH";
  // A program that is only installed, not built, is still found (the shim does not hide the rest of the system).
  EXPECT_EQ(t.run_by_name("fleetwm-settings"), "OLD-installed-settings");
}

TEST(PgoTrainingShim, RunningItTwiceChangesNothing) {
  TrTree t;
  ASSERT_EQ(tr_run(t.shim_cmd()), "");
  ASSERT_EQ(tr_run(t.shim_cmd()), "");
  EXPECT_EQ(t.run_by_name("fleetwm-bar"), "built-bar");
  int links = 0;
  for (const auto& e : tr_fs::directory_iterator(t.shim)) links += tr_fs::is_symlink(e.path());
  EXPECT_EQ(links, 5);
}

TEST(PgoTrainingShim, ExplainsAMissingArgument) {
  const std::string out = tr_run("bash '" + (tr_root() / "scripts/pgo-path-shim.sh").string() + "'");
  EXPECT_NE(out.find("usage"), std::string::npos);
}

TEST(PgoTraining, PutsTheShimFirstOnPathBeforeAnythingStarts) {
  const std::string script = tr_read(tr_root() / "scripts/pgo-train-session.sh");
  const size_t shim = script.find("pgo-path-shim.sh\" \"$BUILD_DIR\" \"$SHIM_DIR\"");
  const size_t path = script.find("export PATH=\"${SHIM_DIR}:${PATH}\"");
  const size_t compositor = script.find("\"${BUILD_DIR}/src/compositor/fleetwm\" >");
  ASSERT_NE(shim, std::string::npos);
  ASSERT_NE(path, std::string::npos);
  ASSERT_NE(compositor, std::string::npos);
  EXPECT_LT(shim, path);
  EXPECT_LT(path, compositor) << "the compositor autostarts the bar by name, so PATH must be set before it starts";
}

TEST(PgoTraining, NeverStartsAnInstalledCopyOrASecondOne) {
  const std::string script = tr_read(tr_root() / "scripts/pgo-train-session.sh");
  EXPECT_EQ(script.find("/usr/local"), std::string::npos) << "the training must not name an installed path";
  EXPECT_EQ(script.find("FLEETWM_BINDIR"), std::string::npos);
  for (const char* program : {"src/bar/fleetwm-bar", "src/wallpaper/fleetwm-wallpaper", "apps/lockapplet/fleetwm-lockapplet"}) {
    EXPECT_NE(script.find(std::string("ensure_running \"${BUILD_DIR}/") + program + "\""), std::string::npos)
        << program << " must be started only when the compositor's autostart did not";
    EXPECT_EQ(script.find(std::string("\nspawn_client \"${BUILD_DIR}/") + program + "\""), std::string::npos)
        << program << " is autostarted by the compositor; starting it again would run two";
  }
  EXPECT_NE(script.find("readlink"), std::string::npos) << "ensure_running compares the real executable, not a name";
}

// ---- a plain `kill` must end every program through its normal exit path -------------------------------------
//
// Found by the training run: with PipeWire running, Settings and the bar died on SIGTERM without writing their
// profile, because PipeWire's thread existed before the signals were blocked (exit status 143 instead of 0).

#include <pthread.h>
#include <signal.h>

#include <thread>

#include "quit_signals.hpp"

TEST(QuitSignals, ThreadsStartedAfterTheBlockInheritIt) {
  sigset_t old;
  pthread_sigmask(SIG_SETMASK, nullptr, &old);
  fleetwm::block_quit_signals();
  bool term_blocked = false, int_blocked = false;
  std::thread t([&] {
    sigset_t m;
    pthread_sigmask(SIG_SETMASK, nullptr, &m);
    term_blocked = sigismember(&m, SIGTERM) == 1;
    int_blocked = sigismember(&m, SIGINT) == 1;
  });
  t.join();
  pthread_sigmask(SIG_SETMASK, &old, nullptr);
  EXPECT_TRUE(term_blocked);
  EXPECT_TRUE(int_blocked);
}

TEST(QuitSignals, EveryProgramBlocksThemFirstThing) {
  for (const char* file : {"src/bar/main.cpp", "apps/settings/main.cpp", "src/wallpaper/main.cpp", "apps/langpicker/main.cpp",
                           "apps/powermenu/main.cpp", "apps/ctxmenu/main.cpp", "src/locker/main.cpp", "src/greeter-login/main.cpp",
                           "apps/audiomixer/main.cpp", "apps/launcher/main.cpp", "apps/shortcuts/main.cpp"}) {
    const std::string src = tr_read(tr_root() / file);
    const size_t main_at = src.find("\nint main(");
    ASSERT_NE(main_at, std::string::npos) << file;
    const size_t block_at = src.find("fleetwm::block_quit_signals();", main_at);
    ASSERT_NE(block_at, std::string::npos) << file << " never calls block_quit_signals()";
    const size_t body = src.find('{', main_at);
    EXPECT_LT(block_at - body, 4u + std::string("\n  fleetwm::block_quit_signals();").size()) << file << ": it must be the first statement of main()";
  }
}

// ---- what the training drives (so a later edit cannot quietly drop it) -------------------------------------

namespace {
std::string tr_script() { return tr_read(tr_root() / "scripts/pgo-train-session.sh"); }
size_t tr_count(const std::string& hay, const std::string& needle) {
  size_t n = 0;
  for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + needle.size())) ++n;
  return n;
}
}  // namespace

TEST(PgoTrainingCoverage, KeyChordsUseWtypeModifiersNotModifierKeyPresses) {
  const std::string script = tr_script();
  // `wtype -P Super_L` presses the key but leaves the modifier state empty, so no shortcut ever fired (found 2026-10-07).
  EXPECT_EQ(script.find("keys -P"), std::string::npos) << "use -M/-m for modifiers";
  EXPECT_EQ(script.find("-p Alt_L"), std::string::npos);
  EXPECT_EQ(script.find("-p Super_L"), std::string::npos);
  EXPECT_NE(script.find("keys -M logo -k \"$1\" -m logo"), std::string::npos) << "snap";
  EXPECT_NE(script.find("maximize_key()  { keys -M alt -k F10 -m alt; }"), std::string::npos);
}

TEST(PgoTrainingCoverage, EveryThemeCornerStyleFrameWidthBarLayoutPowerModeAndTaskbarEdge) {
  const std::string script = tr_script();
  for (const char* theme : {"dark", "light", "catppuccin", "dracula", "oled_black"})
    EXPECT_NE(script.find(std::string(" ") + theme + "\""), std::string::npos) << theme;
  for (const char* what : {"CORNERS=(rounded square", "BAR_LAYOUTS=(full island capsules", "POWER_MODES=(normal performance battery_saver"})
    EXPECT_NE(script.find(what), std::string::npos) << what;
  std::smatch m;
  ASSERT_TRUE(std::regex_search(script, m, std::regex(R"(EDGES=\(([^)]*)\))")));
  const std::string edges = m[1];
  for (const char* edge : {"bottom", "top", "left", "right"}) EXPECT_NE(edges.find(edge), std::string::npos) << edge;
  ASSERT_TRUE(std::regex_search(script, m, std::regex(R"(FRAMES=\(([^)]*)\))")));
  const std::string frames = m[1];
  EXPECT_NE(frames.find(" 0 "), std::string::npos) << "a round without a frame";
  EXPECT_NE(frames.find("12"), std::string::npos) << "a wide frame";
}

TEST(PgoTrainingCoverage, EveryIconStateIsDrawn) {
  const std::string script = tr_script();
  // Batteries: sweeping, full, under 10%, middle, almost empty and charging, none. Network: every kind in every state.
  for (const char* state : {"icon_state 40 Charging", "icon_state 100 Full", "icon_state 8 Discharging", "icon_state 55 Discharging",
                            "icon_state 5 Charging", "icon_state none"})
    EXPECT_NE(script.find(state), std::string::npos) << state;
  for (const char* nic : {"wifi:up", "wifi:down", "wired:up", "wired:unplugged", "mobile:up", "mobile:down"})
    EXPECT_NE(script.find(nic), std::string::npos) << nic;
  EXPECT_NE(script.find("FLEETWM_BATTERY_DIR"), std::string::npos);
  EXPECT_NE(script.find("FLEETWM_SYS_NET"), std::string::npos);
  EXPECT_NE(script.find("wpctl set-volume"), std::string::npos) << "volume levels through a PipeWire null sink";
  EXPECT_NE(script.find("wpctl set-mute"), std::string::npos);
  EXPECT_GE(tr_count(script, "icon_state "), 8u);
}

TEST(PgoTrainingCoverage, WindowsAreDraggedResizedSnappedAndTheirCaptionButtonsClicked) {
  const std::string script = tr_script();
  for (const char* what : {"ptr drag", "ptr dclick", "ptr rclick", "caption_buttons", "caption_at", "phase_window_elements",
                           "phase_taskbar_pointer", "phase_tiling", "phase_workspaces", "phase_small_screen", "OUTPUT_SET",
                           "tiling_shift p", "tiling_shift f", "ws_send_key", "WORKSPACE "})
    EXPECT_NE(script.find(what), std::string::npos) << what;
  // Every edge and corner of a window: right, left, bottom, top, and the four corners.
  EXPECT_GE(tr_count(script, "ptr drag"), 14u);
}

TEST(PgoTrainingCoverage, ThePointerHelperIsBuiltOnTheFlyAndItsProtocolIsVendored) {
  const std::string script = tr_script();
  EXPECT_NE(script.find("wayland-scanner client-header"), std::string::npos);
  EXPECT_NE(script.find("pgo-pointer.c"), std::string::npos);
  EXPECT_TRUE(tr_fs::exists(tr_root() / "scripts/pgo-pointer.c"));
  EXPECT_TRUE(tr_fs::exists(tr_root() / "scripts/wlr-virtual-pointer-unstable-v1.xml"));
  EXPECT_NE(script.find("have wayland-scanner && have cc"), std::string::npos) << "without them the pointer part is skipped, not an error";
}

TEST(PgoTrainingCoverage, ACrashOfOneOfOurOwnProgramsIsReported) {
  const std::string script = tr_script();
  EXPECT_NE(script.find("WARNING: "), std::string::npos);
  EXPECT_NE(script.find("crashes.log"), std::string::npos);
}
