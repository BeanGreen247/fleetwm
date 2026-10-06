// Popups and sound: where the volume mixer opens (next to the bar or taskbar it came from, not always the
// top right), that a second click closes it instead of stacking another, and that the installer puts in
// everything sound needs (the PulseAudio bridge programs talk to, and the device profiles).
#include <gtest/gtest.h>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "bar_config.hpp"
#include "popup_namespaces.hpp"
#include "popup_spot.hpp"
#include "single_instance.hpp"
#include "theme.hpp"

namespace {

namespace pp_fs = std::filesystem;

std::string pp_read(const pp_fs::path& p) {
  std::ifstream in(p);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

pp_fs::path pp_root() { return pp_fs::path(FLEETWM_SOURCE_DIR); }

}  // namespace

// ---- where the mixer opens -------------------------------------------------------------------------

TEST(PopupSpot, DesktopLayoutOpensOnTheTaskbarsEdgeNotTheOtherSideOfTheScreen) {
  using namespace fleetwm;
  // The reported bug: the taskbar is at the bottom and the mixer appeared at the top.
  const PopupSpot bottom = popup_spot_beside_bar(WindowLayout::Desktop, BarLayout::Capsules, TaskbarPosition::Bottom);
  EXPECT_TRUE(bottom.anchor & kAnchorBottom);
  EXPECT_FALSE(bottom.anchor & kAnchorTop) << "a bottom taskbar must not get a popup at the top";
  EXPECT_TRUE(bottom.anchor & kAnchorRight) << "the volume readout is on the right";
  EXPECT_GE(bottom.bottom, kTaskbarThickness) << "it must clear the taskbar";

  const PopupSpot top = popup_spot_beside_bar(WindowLayout::Desktop, BarLayout::Full, TaskbarPosition::Top);
  EXPECT_TRUE(top.anchor & kAnchorTop);
  EXPECT_FALSE(top.anchor & kAnchorBottom);
  EXPECT_GE(top.top, kTaskbarThickness);

  const PopupSpot left = popup_spot_beside_bar(WindowLayout::Desktop, BarLayout::Full, TaskbarPosition::Left);
  EXPECT_TRUE(left.anchor & kAnchorLeft);
  EXPECT_FALSE(left.anchor & kAnchorRight);
  EXPECT_GE(left.left, kTaskbarWidth);

  const PopupSpot right = popup_spot_beside_bar(WindowLayout::Desktop, BarLayout::Full, TaskbarPosition::Right);
  EXPECT_TRUE(right.anchor & kAnchorRight);
  EXPECT_FALSE(right.anchor & kAnchorLeft);
  EXPECT_GE(right.right, kTaskbarWidth);
}

TEST(PopupSpot, TilingLayoutOpensUnderTheTopBarInEveryBarStyle) {
  using namespace fleetwm;
  for (BarLayout style : {BarLayout::Full, BarLayout::Capsules, BarLayout::Island}) {
    const PopupSpot s = popup_spot_beside_bar(WindowLayout::Tiling, style, TaskbarPosition::Bottom);
    EXPECT_TRUE(s.anchor & kAnchorTop) << bar_layout_to_string(style);
    EXPECT_TRUE(s.anchor & kAnchorRight);
    EXPECT_FALSE(s.anchor & kAnchorBottom) << "the taskbar position means nothing in the Tiling layout";
    EXPECT_GE(s.top, kBarHeight) << "must clear the bar";
  }
  // Capsules and Island float below the screen edge, so the popup sits that much lower.
  const int full = popup_spot_beside_bar(WindowLayout::Tiling, BarLayout::Full, TaskbarPosition::Top).top;
  EXPECT_EQ(popup_spot_beside_bar(WindowLayout::Tiling, BarLayout::Capsules, TaskbarPosition::Top).top, full + kCapsuleTopMargin);
  EXPECT_EQ(popup_spot_beside_bar(WindowLayout::Tiling, BarLayout::Island, TaskbarPosition::Top).top, full + kIslandTopMargin);
}

TEST(PopupSpot, NeverPinsABothOppositeEdgesAndAlwaysTouchesTheScreenCorner) {
  using namespace fleetwm;
  for (WindowLayout layout : {WindowLayout::Tiling, WindowLayout::Desktop})
    for (BarLayout style : {BarLayout::Full, BarLayout::Capsules, BarLayout::Island})
      for (TaskbarPosition pos : {TaskbarPosition::Top, TaskbarPosition::Bottom, TaskbarPosition::Left, TaskbarPosition::Right}) {
        const PopupSpot s = popup_spot_beside_bar(layout, style, pos);
        const std::string what = std::string(layout == WindowLayout::Desktop ? "desktop " : "tiling ") + bar_layout_to_string(style) +
                                 " " + taskbar_position_to_string(pos);
        EXPECT_FALSE((s.anchor & kAnchorTop) && (s.anchor & kAnchorBottom)) << what;
        EXPECT_FALSE((s.anchor & kAnchorLeft) && (s.anchor & kAnchorRight)) << what;
        EXPECT_NE(s.anchor & (kAnchorTop | kAnchorBottom), 0u) << what;
        EXPECT_NE(s.anchor & (kAnchorLeft | kAnchorRight), 0u) << what;
        EXPECT_GE(s.top, 0) << what;
        EXPECT_GE(s.right, 0) << what;
        EXPECT_GE(s.bottom, 0) << what;
        EXPECT_GE(s.left, 0) << what;
      }
}

TEST(PopupSpot, TheBarAndTheMixerShareOneSetOfSizes) {
  const std::string bar = pp_read(pp_root() / "src/bar/main.cpp");
  const std::string mixer = pp_read(pp_root() / "apps/audiomixer/main.cpp");
  for (const char* name : {"kBarHeight", "kCapsuleTopMargin", "kIslandTopMargin"}) {
    EXPECT_EQ(bar.find(std::string("constexpr int ") + name), std::string::npos) << "the bar defines its own " << name;
    EXPECT_EQ(mixer.find(std::string("constexpr int ") + name), std::string::npos) << "the mixer defines its own " << name << " (it had 24 where the bar is 30)";
  }
  EXPECT_NE(mixer.find("popup_spot_beside_bar"), std::string::npos) << "the mixer must be placed by the shared rule";
  EXPECT_EQ(mixer.find("margin_top = kBarHeight"), std::string::npos) << "no hard-coded top-right position";
}

// ---- a second click closes the mixer -----------------------------------------------------------------

namespace {

struct PpChild {
  pid_t pid = -1;
  explicit PpChild(const char* program) {
    pid = fork();
    if (pid == 0) {
      execlp(program, program, "30", nullptr);
      _exit(127);
    }
    for (int i = 0; i < 200; ++i) {  // wait until the new process shows up under its real name
      std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
      std::string name;
      if (comm >> name && name == program) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  ~PpChild() {
    if (pid > 0) {
      kill(pid, SIGKILL);
      waitpid(pid, nullptr, 0);
    }
  }
  bool terminated_by_sigterm() {
    int status = 0;
    for (int i = 0; i < 400; ++i) {
      if (waitpid(pid, &status, WNOHANG) == pid) {
        pid = -1;
        return WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
  }
};

std::string pp_pid_file(const std::string& tag) {
  return (pp_fs::temp_directory_path() / ("fleetwm-single-instance-" + tag + "-" + std::to_string(getpid()) + ".pid")).string();
}

}  // namespace

TEST(SingleInstance, ASecondLaunchClosesTheRunningOneAndSaysSo) {
  PpChild running("sleep");
  ASSERT_GT(running.pid, 0);
  const std::string file = pp_pid_file("toggle");
  std::ofstream(file) << running.pid << "\n";
  EXPECT_TRUE(fleetwm::toggle_running_instance(file, "sleep")) << "the caller must exit instead of opening another window";
  EXPECT_TRUE(running.terminated_by_sigterm());
  std::remove(file.c_str());
}

TEST(SingleInstance, ANumberThatNowBelongsToSomethingElseIsLeftAlone) {
  PpChild other("sleep");
  ASSERT_GT(other.pid, 0);
  const std::string file = pp_pid_file("foreign");
  std::ofstream(file) << other.pid << "\n";
  EXPECT_FALSE(fleetwm::toggle_running_instance(file, "fleetwm-audiomi")) << "a recycled pid must never be killed";
  EXPECT_EQ(kill(other.pid, 0), 0) << "the other process must still be running";
  std::remove(file.c_str());
}

TEST(SingleInstance, StaleMissingAndOwnPidFilesDoNothing) {
  const std::string file = pp_pid_file("stale");
  EXPECT_FALSE(fleetwm::toggle_running_instance(file, "sleep")) << "no file";
  std::ofstream(file) << "99999999\n";
  EXPECT_FALSE(fleetwm::toggle_running_instance(file, "sleep")) << "a process that is gone";
  std::ofstream(file) << getpid() << "\n";
  EXPECT_FALSE(fleetwm::toggle_running_instance(file, "sleep")) << "a program must not close itself";
  std::ofstream(file) << "not a number\n";
  EXPECT_FALSE(fleetwm::toggle_running_instance(file, "sleep"));
  std::ofstream(file) << "1\n";
  EXPECT_FALSE(fleetwm::toggle_running_instance(file, "systemd")) << "pid 1 is never a candidate";
  std::remove(file.c_str());
}

TEST(SingleInstance, PidFileRoundTripAndRemovalOnlyOfYourOwn) {
  const std::string file = pp_pid_file("own");
  fleetwm::write_pid_file(file);
  EXPECT_EQ(pp_read(file), std::to_string(getpid()) + "\n");
  fleetwm::remove_pid_file(file);
  EXPECT_FALSE(pp_fs::exists(file));
  std::ofstream(file) << "12345\n";  // a newer instance wrote its own
  fleetwm::remove_pid_file(file);
  EXPECT_TRUE(pp_fs::exists(file)) << "the file of another instance must stay";
  std::remove(file.c_str());
}

TEST(SingleInstance, PidFileLivesInTheRuntimeDirectory) {
  setenv("XDG_RUNTIME_DIR", "/run/user/4242", 1);
  EXPECT_EQ(fleetwm::single_instance_pid_file("fleetwm-audiomixer"), "/run/user/4242/fleetwm-audiomixer.pid");
  unsetenv("XDG_RUNTIME_DIR");
  EXPECT_EQ(fleetwm::single_instance_pid_file("x"), "/tmp/x.pid");
}

TEST(SingleInstance, TheMixerUsesItUnderItsRealProcessName) {
  const std::string mixer = pp_read(pp_root() / "apps/audiomixer/main.cpp");
  EXPECT_NE(mixer.find("toggle_running_instance(pid_file, \"fleetwm-audiomi\")"), std::string::npos)
      << "the process name in /proc is the first 15 characters of fleetwm-audiomixer";
  EXPECT_NE(mixer.find("write_pid_file(pid_file)"), std::string::npos);
  EXPECT_NE(mixer.find("remove_pid_file(pid_file)"), std::string::npos);
  EXPECT_EQ(std::string("fleetwm-audiomixer").substr(0, 15), "fleetwm-audiomi");
}

// ---- the installer puts in what sound needs ------------------------------------------------------------

TEST(AudioInstaller, InstallsTheSoundServerItsBridgesAndTheDeviceProfiles) {
  const std::string install = pp_read(pp_root() / "install.sh");
  ASSERT_FALSE(install.empty());
  struct Need {
    const char* package;
    const char* why;
  };
  for (const Need& n : {Need{"pipewire", "the sound server"}, Need{"wireplumber", "decides which device to use"},
                        Need{"pipewire-pulse", "browsers and players talk PulseAudio and find no sound server without it"},
                        Need{"pipewire-alsa", "programs that use ALSA directly"},
                        Need{"alsa-ucm-conf", "device profiles: without them an Intel SOF/ES8336 laptop stays silent on the stereo-fallback profile"},
                        Need{"alsa-utils", "amixer and alsactl"}}) {
    std::istringstream lines(install);
    std::string line;
    bool found = false;
    while (std::getline(lines, line)) {
      if (line.rfind("apt_install", 0) != 0) continue;
      std::istringstream words(line);
      for (std::string w; words >> w;) found = found || w == n.package;
    }
    EXPECT_TRUE(found) << n.package << " is not installed by install.sh (" << n.why << ")";
  }
}

TEST(AudioInstaller, TheDocsListTheSamePackages) {
  const std::string docs = pp_read(pp_root() / "docs/DEVELOPMENT.md");
  for (const char* package : {"pipewire-pulse", "pipewire-alsa", "alsa-ucm-conf", "alsa-utils"})
    EXPECT_NE(docs.find(package), std::string::npos) << package << " is installed but not in the dependency list in docs/DEVELOPMENT.md";
}

// ---- popups close on a press outside them ---------------------------------------------------------------

TEST(PopupDismissal, TheMixerAndTheStartMenuCloseOnAnOutsideClickAndNothingElseDoes) {
  using fleetwm::dismisses_on_outside_click;
  EXPECT_TRUE(dismisses_on_outside_click("fleetwm-audiomixer"));
  EXPECT_TRUE(dismisses_on_outside_click("fleetwm-start-menu"));
  for (const char* other : {"fleetwm-bar", "fleetwm-wallpaper", "fleetwm-launcher", "fleetwm-powermenu", "fleetwm-locker", "fleetwm-lockapplet",
                            "fleetwm-audiomixer2", "fleetwm-audiomix", "", "waybar", "gtk-layer-shell"})
    EXPECT_FALSE(dismisses_on_outside_click(other)) << "'" << other << "' must not be closed by clicks elsewhere";
  EXPECT_FALSE(dismisses_on_outside_click(nullptr));
}

TEST(PopupDismissal, TheCompositorUsesTheSharedListAndSwallowsThePress) {
  const std::string server = pp_read(pp_root() / "src/compositor/server.cpp");
  EXPECT_NE(server.find("dismisses_on_outside_click(menu->namespace_)"), std::string::npos)
      << "the compositor must decide which popups close from the shared list";
  EXPECT_EQ(server.find("\"fleetwm-start-menu\""), std::string::npos) << "a second, private list of popup names";
  const size_t at = server.find("dismisses_on_outside_click(menu->namespace_)");
  ASSERT_NE(at, std::string::npos);
  const std::string body = server.substr(at, 900);
  EXPECT_NE(body.find("wlr_layer_surface_v1_destroy(menu)"), std::string::npos) << "the popup must be told to close";
  EXPECT_NE(body.find("swallow_release = true"), std::string::npos) << "the press that closes it must not also click what is underneath";
  EXPECT_NE(body.find("return;"), std::string::npos);
  // A press inside the popup itself must still reach it.
  EXPECT_NE(body.find("wlr_surface_get_root_surface(hit.surface) == menu->surface"), std::string::npos);
}

TEST(PopupDismissal, EachPopupProgramUsesItsNamespaceFromTheSharedHeader) {
  EXPECT_NE(pp_read(pp_root() / "apps/audiomixer/main.cpp").find("cfg.name = fleetwm::kAudioMixerNamespace"), std::string::npos)
      << "if the mixer's layer name drifts from the list, clicks outside stop closing it";
  EXPECT_NE(pp_read(pp_root() / "apps/launcher/main.cpp").find("cfg.name = fleetwm::kStartMenuNamespace"), std::string::npos);
}

TEST(AudioInstaller, AppliesTheDeviceBootSequenceSoSoundWorksWithoutAReboot) {
  const std::string install = pp_read(pp_root() / "install.sh");
  const size_t packages = install.find("apt_install pipewire-pulse pipewire-alsa alsa-ucm-conf alsa-utils");
  ASSERT_NE(packages, std::string::npos);
  const size_t init = install.find("sudo alsactl init", packages);
  const size_t store = install.find("sudo alsactl store", packages);
  const size_t restart = install.find("systemctl --user try-restart wireplumber.service", packages);
  ASSERT_NE(init, std::string::npos) << "the UCM BootSequence (output mixers on, volumes) is only applied by alsactl init";
  ASSERT_NE(store, std::string::npos) << "the result must be saved so the next boot restores it";
  ASSERT_NE(restart, std::string::npos) << "WirePlumber has to re-apply the profile on top of the new mixer state";
  EXPECT_LT(init, store);
  EXPECT_LT(store, restart);
  // all of it only on a machine that has a sound card, and none of it may stop the install
  const size_t guard = install.rfind("aplay -l", init);
  ASSERT_NE(guard, std::string::npos);
  EXPECT_LT(packages, guard);
  EXPECT_LT(guard, init) << "the sound-card check comes before alsactl";
  for (size_t at : {init, store, restart}) {
    const size_t eol = install.find('\n', at);
    EXPECT_NE(install.substr(at, eol - at).find("|| true"), std::string::npos) << install.substr(at, eol - at);
  }
  EXPECT_NE(install.find("fi", restart), std::string::npos);
}

TEST(AudioInstaller, TheSoundNotesExplainTheCauseAndHowToCheck) {
  const std::string notes = pp_read(pp_root() / "docs/AUDIO.md");
  ASSERT_FALSE(notes.empty());
  for (const char* what : {"alsactl init", "alsa-ucm-conf", "BootSequence", "ES8336", "wpctl status", "amixer", "pipewire-pulse"})
    EXPECT_NE(notes.find(what), std::string::npos) << "docs/AUDIO.md should mention " << what;
}
