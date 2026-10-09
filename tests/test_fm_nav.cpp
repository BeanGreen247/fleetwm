#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>

#include "nav_tree.hpp"

using namespace fleetwm::fm;
namespace fs = std::filesystem;

namespace {
class FmNav : public ::testing::Test {
 protected:
  void SetUp() override {
    home_ = fs::temp_directory_path() / ("fm-nav-" + std::to_string(::getpid()) + "-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
    for (const char* d : {"Desktop", "Documents", "Downloads", "Music", "Pictures", "Videos", "projects/alpha", "projects/beta"}) fs::create_directories(home_ / d);
  }
  void TearDown() override {
    std::error_code ec;
    fs::remove_all(home_, ec);
  }
  static Volume vol(const std::string& dev, const std::string& mp, DriveKind k, bool mounted = true) {
    Volume v;
    v.device = dev;
    v.mountpoint = mounted ? mp : "";
    v.kind = k;
    v.mounted = mounted;
    v.ejectable = k == DriveKind::Removable;
    v.total = 100ull << 30;
    v.free = 40ull << 30;
    v.label = k == DriveKind::Removable ? "Stick" : "";
    v.disk = "/dev/sdb";
    return v;
  }
  NavInput input(ViewStyle style) {
    settings_ = FmSettings{};
    NavInput in;
    in.settings = &settings_;
    in.style = style;
    in.home = home_.string();
    in.volumes = {vol("/dev/sda1", "/", DriveKind::Internal), vol("/dev/sdb1", "/media/me/Stick", DriveKind::Removable)};
    in.places = {{"NAS media", "smb://nas/media"}};
    in.subfolders = [](const std::string& p) {
      std::vector<std::string> out;
      std::error_code ec;
      for (const auto& e : fs::directory_iterator(p, ec))
        if (e.is_directory(ec) && e.path().filename().string()[0] != '.') out.push_back(e.path().filename().string());
      std::sort(out.begin(), out.end());
      return out;
    };
    return in;
  }
  static const NavRow* find(const NavTree& t, const std::string& label) {
    for (const NavRow& r : t.rows())
      if (r.label == label) return &r;
    return nullptr;
  }
  fs::path home_;
  FmSettings settings_;
};
}  // namespace

TEST_F(FmNav, Windows7TreeHasFavoritesLibrariesComputerNetwork) {
  NavTree t;
  t.build(input(ViewStyle::Windows7));
  std::vector<std::string> heads;
  for (const NavRow& r : t.rows())
    if (r.header) heads.push_back(r.label);
  EXPECT_EQ(heads, (std::vector<std::string>{"Favorites", "Libraries", "Computer", "Network"}));
  ASSERT_TRUE(find(t, "Downloads"));
  EXPECT_EQ(find(t, "Downloads")->depth, 1);
  EXPECT_EQ(find(t, "Documents")->icon, IconKind::Documents);
  EXPECT_EQ(find(t, "Recent Places")->address, "recent:///");
  EXPECT_EQ(find(t, "Computer")->address, "computer:///");
  EXPECT_EQ(find(t, "Network")->address, "network:///");
  EXPECT_EQ(find(t, "Trash")->address, "trash:///");
  const NavRow* nas = find(t, "NAS media");
  ASSERT_TRUE(nas);
  EXPECT_EQ(nas->address, "smb://nas/media");
  EXPECT_EQ(nas->depth, 1);
}

TEST_F(FmNav, DrivesCarrySpaceAndEjectFlags) {
  NavTree t;
  t.build(input(ViewStyle::Windows7));
  const NavRow* root = find(t, "Local Disk (/)");
  ASSERT_TRUE(root);
  EXPECT_FALSE(root->eject);
  EXPECT_NEAR(root->used_fraction, 0.6, 1e-9);
  EXPECT_EQ(root->sub, "40.0 GB free of 100 GB");
  const NavRow* stick = find(t, "Stick (sdb1)");
  ASSERT_TRUE(stick);
  EXPECT_TRUE(stick->eject);
  EXPECT_EQ(stick->icon, IconKind::DriveUsb);
  EXPECT_EQ(stick->address, "/media/me/Stick");
}

TEST_F(FmNav, UnmountedDrivesOpenByMountRequest) {
  NavInput in = input(ViewStyle::Windows7);
  in.volumes.push_back(vol("/dev/sdc1", "", DriveKind::Removable, false));
  NavTree t;
  t.build(in);
  const NavRow* r = find(t, "Stick (sdc1)");
  ASSERT_TRUE(r);
  EXPECT_EQ(r->address, "mount:/dev/sdc1");
  EXPECT_FALSE(r->expandable);
  settings_.show_unmounted_removable = false;
  t.build(in);
  EXPECT_FALSE(find(t, "Stick (sdc1)"));
}

TEST_F(FmNav, ExpandingAVolumeListsItsFolders) {
  NavInput in = input(ViewStyle::Windows7);
  in.volumes = {vol("/dev/sda1", home_.string(), DriveKind::Internal)};
  NavTree t;
  t.build(in);
  const std::string key = home_.string();
  EXPECT_FALSE(find(t, "projects")) << "collapsed";
  ASSERT_TRUE(t.toggle(key));
  t.build(in);
  ASSERT_TRUE(find(t, "projects"));
  EXPECT_EQ(find(t, "projects")->depth, 2);
  EXPECT_TRUE(find(t, "projects")->expandable);
  ASSERT_TRUE(t.toggle((home_ / "projects").string()));
  t.build(in);
  EXPECT_TRUE(find(t, "alpha"));
  EXPECT_EQ(find(t, "alpha")->depth, 3);
  ASSERT_TRUE(t.toggle(key));
  t.build(in);
  EXPECT_FALSE(find(t, "projects"));
  EXPECT_FALSE(t.toggle("no-such-key"));
}

TEST_F(FmNav, ExpandToCurrentFolderOpensTheBranch) {
  NavInput in = input(ViewStyle::Windows7);
  in.volumes = {vol("/dev/sda1", home_.string(), DriveKind::Internal)};
  in.current = (home_ / "projects/alpha").string();
  NavTree t;
  t.build(in);
  EXPECT_FALSE(find(t, "projects")) << "off by default";
  settings_.nav_expand_to_current = true;
  t.build(in);
  EXPECT_TRUE(find(t, "projects"));
  ASSERT_GE(t.row_for_address(in.current), 0) << "the whole chain down to the open folder is expanded";
  EXPECT_EQ(t.rows()[t.row_for_address(in.current)].label, "alpha");
  EXPECT_TRUE(find(t, "beta")) << "its siblings are visible too";
}

TEST_F(FmNav, SectionsCanBeHidden) {
  NavInput in = input(ViewStyle::Windows7);
  settings_.nav_show_favorites = false;
  settings_.nav_show_libraries = false;
  settings_.nav_show_network = false;
  settings_.nav_show_trash = false;
  NavTree t;
  t.build(in);
  EXPECT_FALSE(find(t, "Favorites"));
  EXPECT_FALSE(find(t, "Libraries"));
  EXPECT_FALSE(find(t, "Network"));
  EXPECT_FALSE(find(t, "Trash"));
  EXPECT_TRUE(find(t, "Computer"));
}

TEST_F(FmNav, MissingUserFoldersAreLeftOut) {
  fs::remove_all(home_ / "Music");
  NavTree t;
  t.build(input(ViewStyle::Windows7));
  EXPECT_FALSE(find(t, "Music"));
  EXPECT_TRUE(find(t, "Pictures"));
}

TEST_F(FmNav, SidebarStylesAreFlatWithPlacesDevicesNetwork) {
  for (ViewStyle s : {ViewStyle::Caja, ViewStyle::Nemo, ViewStyle::Nautilus, ViewStyle::Thunar, ViewStyle::PcManFm, ViewStyle::Dolphin}) {
    NavTree t;
    t.build(input(s));
    std::vector<std::string> heads;
    for (const NavRow& r : t.rows()) {
      if (r.header) heads.push_back(r.label);
      EXPECT_LE(r.depth, 1);
      EXPECT_FALSE(r.expandable);
    }
    EXPECT_EQ(heads, (std::vector<std::string>{"Places", "Devices", "Network"})) << style_key(s);
    EXPECT_TRUE(find(t, "Home")) << style_key(s);
    EXPECT_TRUE(find(t, "Trash")) << style_key(s);
    EXPECT_TRUE(find(t, "Stick (sdb1)")) << style_key(s);
    EXPECT_TRUE(find(t, "NAS media")) << style_key(s);
  }
}

TEST_F(FmNav, FinderStyleNamesItsSections) {
  NavTree t;
  t.build(input(ViewStyle::Mac));
  std::vector<std::string> heads;
  for (const NavRow& r : t.rows())
    if (r.header) heads.push_back(r.label);
  EXPECT_EQ(heads, (std::vector<std::string>{"Favorites", "Locations", "Network"}));
  EXPECT_TRUE(find(t, "Recents"));
}

TEST_F(FmNav, GvfsMountsAreListedAsNetworkDrivesInTheSidebar) {
  NavInput in = input(ViewStyle::Caja);
  Volume g = vol("", "/run/user/1000/gvfs/smb-share:server=nas,share=media", DriveKind::Network);
  g.label = "media (\\\\nas)";
  in.volumes.push_back(g);
  NavTree t;
  t.build(in);
  const NavRow* r = find(t, "media (\\\\nas)");
  ASSERT_TRUE(r);
  EXPECT_TRUE(r->eject);
  EXPECT_EQ(r->icon, IconKind::DriveNetwork);
}

TEST_F(FmNav, RowForAddressFindsExactAndContainingRows) {
  NavTree t;
  t.build(input(ViewStyle::Windows7));
  EXPECT_GE(t.row_for_address("computer:///"), 0);
  EXPECT_EQ(t.rows()[t.row_for_address(home_.string() + "/Downloads")].label, "Downloads");
  EXPECT_EQ(t.rows()[t.row_for_address(home_.string() + "/Downloads/sub/deeper")].label, "Downloads");
  EXPECT_EQ(t.row_for_address("nothing://here"), -1);
}
