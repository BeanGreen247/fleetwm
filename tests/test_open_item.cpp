#include <gtest/gtest.h>

#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

#include "desktop_entry.hpp"
#include "file_ops.hpp"

namespace fleetwm::fm {
namespace {
namespace fs = std::filesystem;

class OpenItem : public ::testing::Test {
 protected:
  void SetUp() override {
    dir = fs::temp_directory_path() / ("fleetwm-open-item-" + std::to_string(::getpid()));
    fs::create_directories(dir);
  }
  void TearDown() override { fs::remove_all(dir); }
  std::string make(const std::string& name, const std::string& text, bool exec) {
    const std::string p = (dir / name).string();
    std::ofstream(p) << text;
    ::chmod(p.c_str(), exec ? 0755 : 0644);
    return p;
  }
  fs::path dir;
};

TEST_F(OpenItem, ExecutableBinaryIsRun) {
  EXPECT_EQ(classify_open(make("game", std::string("\x7f") + "ELF....", true)), OpenKind::Program);
}

TEST_F(OpenItem, ScriptWithShebangRunsInTerminal) {
  EXPECT_EQ(classify_open(make("start.sh", "#!/bin/sh\necho hi\n", true)), OpenKind::Script);
}

TEST_F(OpenItem, NotExecutableStaysWithTheDefaultApplication) {
  EXPECT_EQ(classify_open(make("notes.sh", "#!/bin/sh\n", false)), OpenKind::Default);
  EXPECT_EQ(classify_open(make("game2", std::string("\x7f") + "ELF....", false)), OpenKind::Default);
}

TEST_F(OpenItem, ExecutableTextWithoutShebangIsNotRun) {
  EXPECT_EQ(classify_open(make("readme", "just text\n", true)), OpenKind::Default);
}

TEST_F(OpenItem, SharedLibraryIsNotAProgram) {
  EXPECT_EQ(classify_open(make("libfoo.so", std::string("\x7f") + "ELF....", true)), OpenKind::Default);
  EXPECT_EQ(classify_open(make("libfoo.so.1.2", std::string("\x7f") + "ELF....", true)), OpenKind::Default);
}

TEST_F(OpenItem, ShortcutsAreRecognisedByExtension) {
  EXPECT_EQ(classify_open(make("GTA.desktop", "[Desktop Entry]\n", false)), OpenKind::DesktopEntry);
  EXPECT_EQ(classify_open(make("Launcher.LNK", "L", false)), OpenKind::WinShortcut);
}

TEST_F(OpenItem, SymbolicLinkIsFollowed) {
  const std::string target = make("real", std::string("\x7f") + "ELF....", true);
  const std::string link = (dir / "link").string();
  fs::create_symlink(target, link);
  EXPECT_EQ(classify_open(link), OpenKind::Program);
  EXPECT_EQ(classify_open((dir / "missing").string()), OpenKind::Default);
  EXPECT_EQ(classify_open(dir.string()), OpenKind::Default);
}

TEST_F(OpenItem, DesktopFileOpenedByHandIgnoresMenuFilters) {
  const std::string p = make("hidden.desktop",
                             "[Desktop Entry]\nType=Application\nName=Hidden Game\nExec=env A=1 game %U\nNoDisplay=true\nOnlyShowIn=GNOME;\nTryExec=/no/such/tool\nPath=/tmp\nTerminal=true\n",
                             false);
  kit::DesktopEntry e;
  ASSERT_TRUE(kit::load_desktop_file(p, &e));
  EXPECT_EQ(e.name, "Hidden Game");
  EXPECT_TRUE(e.terminal);
  EXPECT_EQ(e.path, "/tmp");
  const std::vector<std::string> argv = kit::exec_argv(e);
  ASSERT_EQ(argv.size(), 3u);
  EXPECT_EQ(argv[0], "env");
}

TEST_F(OpenItem, NonApplicationDesktopFileIsRejected) {
  kit::DesktopEntry e;
  EXPECT_FALSE(kit::load_desktop_file(make("link.desktop", "[Desktop Entry]\nType=Link\nName=Site\nURL=https://example.org\n", false), &e));
  EXPECT_FALSE(kit::load_desktop_file((dir / "nothing.desktop").string(), &e));
}

}  // namespace
}  // namespace fleetwm::fm
