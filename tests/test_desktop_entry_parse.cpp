// load_desktop_entries(): the entries a directory of .desktop files turns into. The parser skips every translation it does
// not need, so these tests pin down what must survive: the user's own Name[...]/Comment[...], the untranslated fallback,
// hidden entries, and the keys that come after a [Desktop Action] group.
#include <gtest/gtest.h>

#include <stdlib.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

#include "desktop_entry.hpp"

namespace {
namespace fs = std::filesystem;
using fleetwm::kit::DesktopEntry;
using fleetwm::kit::load_desktop_entries;

struct DeEnv {
  std::string keep_home, keep_dirs, keep_lang, keep_lcm;
  fs::path dir;
  DeEnv() {
    dir = fs::temp_directory_path() / ("fleetwm-de-" + std::to_string(getpid()));
    fs::create_directories(dir / "applications");
    keep_home = getenv("XDG_DATA_HOME") ? getenv("XDG_DATA_HOME") : "";
    keep_dirs = getenv("XDG_DATA_DIRS") ? getenv("XDG_DATA_DIRS") : "";
    keep_lang = getenv("LANG") ? getenv("LANG") : "";
    keep_lcm = getenv("LC_MESSAGES") ? getenv("LC_MESSAGES") : "";
    setenv("XDG_DATA_HOME", dir.c_str(), 1);
    setenv("XDG_DATA_DIRS", (dir / "none").c_str(), 1);
    unsetenv("LC_MESSAGES");
  }
  ~DeEnv() {
    setenv("XDG_DATA_HOME", keep_home.c_str(), 1);
    setenv("XDG_DATA_DIRS", keep_dirs.c_str(), 1);
    setenv("LANG", keep_lang.c_str(), 1);
    if (!keep_lcm.empty()) setenv("LC_MESSAGES", keep_lcm.c_str(), 1);
    fs::remove_all(dir);
  }
  void write(const std::string& name, const std::string& body) { std::ofstream(dir / "applications" / name) << body; }
};

const DesktopEntry* de_find(const std::vector<DesktopEntry>& v, const std::string& id) {
  for (const DesktopEntry& e : v)
    if (e.id == id) return &e;
  return nullptr;
}

const char* kDeApp =
    "# comment\n[Desktop Entry]\nType=Application\nName=Files\nName[cs]=Soubory\nName[de_DE]=Dateien\nName[de]=Dateien kurz\n"
    "Name[fr]=Fichiers\nComment=Browse\nComment[de]=Durchsuchen\nGenericName[de]=Manager\nExec=true %U\nIcon=folder\nTerminal=true\n"
    "Categories=Utility;\n\n[Desktop Action new]\nName=New\nExec=false\n";
}  // namespace

TEST(DesktopEntryParse, PlainNameWithoutALocale) {
  DeEnv env;
  setenv("LANG", "C.UTF-8", 1);
  env.write("a.desktop", kDeApp);
  const auto v = load_desktop_entries();
  const DesktopEntry* e = de_find(v, "a.desktop");
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->name, "Files");
  EXPECT_EQ(e->comment, "Browse");
  EXPECT_EQ(e->icon, "folder");
  EXPECT_TRUE(e->terminal);
  EXPECT_EQ(e->exec, "true %U") << "the action's Exec after the next group must not replace the entry's";
}

TEST(DesktopEntryParse, ExactLocaleBeatsTheLanguageAlone) {
  DeEnv env;
  setenv("LANG", "de_DE.UTF-8", 1);
  env.write("a.desktop", kDeApp);
  const DesktopEntry* e = nullptr;
  const auto v = load_desktop_entries();
  e = de_find(v, "a.desktop");
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->name, "Dateien");
  EXPECT_EQ(e->comment, "Durchsuchen") << "falls back from de_DE to de";
}

TEST(DesktopEntryParse, OtherLanguagesAreIgnored) {
  DeEnv env;
  setenv("LANG", "fi_FI.UTF-8", 1);
  env.write("a.desktop", kDeApp);
  const auto v = load_desktop_entries();
  const DesktopEntry* e = de_find(v, "a.desktop");
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->name, "Files");
}

TEST(DesktopEntryParse, HiddenAndNonApplicationsAreDropped) {
  DeEnv env;
  setenv("LANG", "C.UTF-8", 1);
  env.write("hidden.desktop", "[Desktop Entry]\nType=Application\nName=H\nExec=true\nNoDisplay=true\n");
  env.write("link.desktop", "[Desktop Entry]\nType=Link\nName=L\nExec=true\n");
  env.write("noexec.desktop", "[Desktop Entry]\nType=Application\nName=N\n");
  env.write("crlf.desktop", "[Desktop Entry]\r\nType=Application\r\nName=Win\r\nExec=true\r\n");
  const auto v = load_desktop_entries();
  EXPECT_EQ(de_find(v, "hidden.desktop"), nullptr);
  EXPECT_EQ(de_find(v, "link.desktop"), nullptr);
  EXPECT_EQ(de_find(v, "noexec.desktop"), nullptr);
  const DesktopEntry* w = de_find(v, "crlf.desktop");
  ASSERT_NE(w, nullptr);
  EXPECT_EQ(w->name, "Win");
}
