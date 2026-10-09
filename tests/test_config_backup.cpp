#include "config_backup.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "test_util.hpp"

namespace fleetwm {
namespace {

namespace fs = std::filesystem;

class ConfigBackup : public testutil::ScopedConfigHome {
 protected:
  std::string read(const std::string& name) {
    std::ifstream in(dir_ / "fleetwm" / name);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }
  std::vector<fs::path> backups() {
    std::vector<fs::path> out;
    for (const auto& e : fs::directory_iterator(dir_))
      if (e.path().filename().string().rfind("fleetwm.bak-", 0) == 0) out.push_back(e.path());
    return out;
  }
  std::string archive() { return (dir_ / "out.tar.gz").string(); }
  // Builds an archive by hand from files under dir_/src, for the rejection tests.
  void make_archive(const std::string& shell_in_src) {
    fs::create_directories(dir_ / "src");
    const std::string cmd = "cd '" + (dir_ / "src").string() + "' && " + shell_in_src;
    ASSERT_EQ(std::system(cmd.c_str()), 0) << cmd;
  }
};

TEST_F(ConfigBackup, RoundTripRestoresFilesAndKeepsTheOldFolder) {
  write_config("theme.toml", "name = \"dark\"\n");
  write_config("bar.toml", "height = 30\n");
  BackupResult e = export_config(archive());
  ASSERT_TRUE(e.ok) << e.message;
  EXPECT_EQ(e.files.size(), 2u);

  write_config("theme.toml", "name = \"light\"\n");
  write_config("mouse.toml", "speed = 1\n");  // not in the archive: stays
  BackupResult i = import_config(archive());
  ASSERT_TRUE(i.ok) << i.message;
  EXPECT_EQ(read("theme.toml"), "name = \"dark\"\n");
  EXPECT_EQ(read("bar.toml"), "height = 30\n");
  EXPECT_EQ(read("mouse.toml"), "speed = 1\n");
  ASSERT_EQ(backups().size(), 1u);
  EXPECT_EQ(i.backup_dir, backups()[0].string());
  std::ifstream old(backups()[0] / "theme.toml");
  std::stringstream ss;
  ss << old.rdbuf();
  EXPECT_EQ(ss.str(), "name = \"light\"\n");
  EXPECT_FALSE(fs::exists(dir_ / "fleetwm" / "theme.toml.import-tmp"));
}

TEST_F(ConfigBackup, MachineSpecificFilesAreLeftOutUnlessAsked) {
  write_config("theme.toml", "a = 1\n");
  write_config("outputs.toml", "b = 2\n");
  write_config("fleetfm-places.toml", "c = 3\n");
  BackupResult e = export_config(archive());
  ASSERT_TRUE(e.ok);
  EXPECT_EQ(e.files, std::vector<std::string>{"theme.toml"});
  EXPECT_EQ(e.skipped.size(), 2u);

  BackupOptions all;
  all.include_machine_specific = true;
  e = export_config(archive(), all);
  ASSERT_TRUE(e.ok);
  EXPECT_EQ(e.files.size(), 3u);

  // An archive that has them does not overwrite this machine's by default.
  write_config("outputs.toml", "b = 99\n");
  BackupResult i = import_config(archive());
  ASSERT_TRUE(i.ok) << i.message;
  EXPECT_EQ(read("outputs.toml"), "b = 99\n");
  i = import_config(archive(), all);
  ASSERT_TRUE(i.ok) << i.message;
  EXPECT_EQ(read("outputs.toml"), "b = 2\n");
}

TEST_F(ConfigBackup, DamagedFileIsNotExportedAndStrayFilesAreIgnored) {
  write_config("theme.toml", "a = 1\n");
  write_config("bar.toml", "this is = = not toml\n");
  write_config("notes.txt", "private\n");
  BackupResult e = export_config(archive());
  ASSERT_TRUE(e.ok);
  EXPECT_EQ(e.files, std::vector<std::string>{"theme.toml"});
  ASSERT_EQ(e.skipped.size(), 1u);
  EXPECT_NE(e.skipped[0].find("bar.toml"), std::string::npos);
}

TEST_F(ConfigBackup, NothingToExport) {
  EXPECT_FALSE(export_config(archive()).ok);
  EXPECT_FALSE(fs::exists(archive()));
}

TEST_F(ConfigBackup, NewerFormatIsRefusedAndNothingChanges) {
  write_config("theme.toml", "a = 1\n");
  fs::create_directories(dir_ / "src/fleetwm");
  std::ofstream(dir_ / "src/fleetwm/manifest.toml") << "format = 99\n";
  std::ofstream(dir_ / "src/fleetwm/theme.toml") << "a = 2\n";
  make_archive("tar -czf '" + archive() + "' fleetwm");
  BackupResult i = import_config(archive());
  EXPECT_FALSE(i.ok);
  EXPECT_NE(i.message.find("newer"), std::string::npos);
  EXPECT_EQ(read("theme.toml"), "a = 1\n");
  EXPECT_TRUE(backups().empty());
}

TEST_F(ConfigBackup, InvalidTomlInArchiveChangesNothing) {
  write_config("theme.toml", "a = 1\n");
  write_config("bar.toml", "b = 1\n");
  fs::create_directories(dir_ / "src/fleetwm");
  std::ofstream(dir_ / "src/fleetwm/manifest.toml") << "format = 1\n";
  std::ofstream(dir_ / "src/fleetwm/theme.toml") << "a = 2\n";
  std::ofstream(dir_ / "src/fleetwm/bar.toml") << "b = = 2\n";
  make_archive("tar -czf '" + archive() + "' fleetwm");
  BackupResult i = import_config(archive());
  EXPECT_FALSE(i.ok);
  EXPECT_EQ(read("theme.toml"), "a = 1\n");
  EXPECT_EQ(read("bar.toml"), "b = 1\n");
  EXPECT_TRUE(backups().empty());
}

TEST_F(ConfigBackup, UnknownOrEscapingEntriesAreRejected) {
  fs::create_directories(dir_ / "src/fleetwm");
  std::ofstream(dir_ / "src/fleetwm/manifest.toml") << "format = 1\n";
  std::ofstream(dir_ / "src/fleetwm/evil.sh") << "x\n";
  make_archive("tar -czf '" + archive() + "' fleetwm");
  EXPECT_FALSE(import_config(archive()).ok);

  fs::remove(dir_ / "src/fleetwm/evil.sh");
  make_archive("tar -czf '" + archive() + "' -P --transform='s,^fleetwm,../x,' fleetwm");
  EXPECT_FALSE(import_config(archive()).ok);
  EXPECT_FALSE(fs::exists(dir_.parent_path() / "x"));
}

TEST_F(ConfigBackup, SymlinkMemberIsRejected) {
  fs::create_directories(dir_ / "src/fleetwm");
  std::ofstream(dir_ / "src/fleetwm/manifest.toml") << "format = 1\n";
  fs::create_symlink("/etc/passwd", dir_ / "src/fleetwm/theme.toml");
  make_archive("tar -czf '" + archive() + "' fleetwm");
  EXPECT_FALSE(import_config(archive()).ok);
  EXPECT_FALSE(fs::exists(dir_ / "fleetwm"));
}

TEST_F(ConfigBackup, ArchiveWithoutManifestAndNonArchiveAreRefused) {
  fs::create_directories(dir_ / "src/fleetwm");
  std::ofstream(dir_ / "src/fleetwm/theme.toml") << "a = 1\n";
  make_archive("tar -czf '" + archive() + "' fleetwm");
  EXPECT_FALSE(import_config(archive()).ok);
  std::ofstream(dir_ / "junk.tar.gz") << "not an archive";
  EXPECT_FALSE(import_config((dir_ / "junk.tar.gz").string()).ok);
  EXPECT_FALSE(import_config((dir_ / "missing.tar.gz").string()).ok);
}

TEST_F(ConfigBackup, ImportIntoAnEmptyHomeCreatesTheFolderWithoutABackup) {
  write_config("theme.toml", "a = 1\n");
  ASSERT_TRUE(export_config(archive()).ok);
  fs::remove_all(dir_ / "fleetwm");
  BackupResult i = import_config(archive());
  ASSERT_TRUE(i.ok) << i.message;
  EXPECT_EQ(read("theme.toml"), "a = 1\n");
  EXPECT_TRUE(backups().empty());
}

}  // namespace
}  // namespace fleetwm
