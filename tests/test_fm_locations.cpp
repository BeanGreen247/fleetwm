#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <functional>

#include "locations.hpp"
#include "test_util.hpp"

using namespace fleetwm::fm;
namespace fs = std::filesystem;

TEST(FmUri, ParsesEveryPart) {
  const Uri u = parse_uri("sftp://me:pw%40x@host.example:2222/home/me/My%20Files?x=1");
  ASSERT_TRUE(u.valid);
  EXPECT_EQ(u.scheme, "sftp");
  EXPECT_EQ(u.user, "me");
  EXPECT_EQ(u.password, "pw@x");
  EXPECT_EQ(u.host, "host.example");
  EXPECT_EQ(u.port, 2222);
  EXPECT_EQ(u.path, "/home/me/My Files");
  EXPECT_EQ(u.query, "x=1");
}

TEST(FmUri, HostOnlyAndIpv6AndRejects) {
  Uri u = parse_uri("ftp://ftp.example.com");
  ASSERT_TRUE(u.valid);
  EXPECT_EQ(u.host, "ftp.example.com");
  EXPECT_EQ(u.path, "");
  EXPECT_EQ(u.port, 0);
  u = parse_uri("sftp://[fe80::1]:22/x");
  ASSERT_TRUE(u.valid);
  EXPECT_EQ(u.host, "fe80::1");
  EXPECT_EQ(u.port, 22);
  EXPECT_FALSE(parse_uri("no-scheme/here").valid);
  EXPECT_FALSE(parse_uri("://x").valid);
  EXPECT_FALSE(parse_uri("ftp://h:99999/").valid);
  EXPECT_FALSE(parse_uri("ftp://h:ab/").valid);
  EXPECT_FALSE(parse_uri("b@d://x").valid);
}

TEST(FmUri, RoundTripsAndHidesThePassword) {
  const Uri u = parse_uri("smb://me:secret@nas/Media%20Share/dir");
  EXPECT_EQ(uri_to_string(u), "smb://me@nas/Media%20Share/dir");
  EXPECT_EQ(uri_to_string(u, true), "smb://me:secret@nas/Media%20Share/dir");
  EXPECT_EQ(uri_to_string(parse_uri("sftp://[::1]:2200/")), "sftp://[::1]:2200/");
  EXPECT_EQ(percent_decode("a%20b%zz%4"), "a b%zz%4");
  EXPECT_EQ(percent_encode("a b/é", true), "a%20b/%C3%A9");
}

TEST(FmLocation, TheAddressBarUnderstandsWindowsAndLinuxSpellings) {
  const std::string home = "/home/me", cwd = "/home/me/docs";
  EXPECT_EQ(parse_location("/etc/", home, cwd).path, "/etc");
  EXPECT_EQ(parse_location("~", home, cwd).path, "/home/me");
  EXPECT_EQ(parse_location("~/x/../y", home, cwd).path, "/home/me/y");
  EXPECT_EQ(parse_location("sub/dir", home, cwd).path, "/home/me/docs/sub/dir");
  EXPECT_EQ(parse_location("../up", home, cwd).path, "/home/me/up");
  EXPECT_EQ(parse_location("  /tmp  ", home, cwd).path, "/tmp");
  EXPECT_EQ(parse_location("file:///tmp/a%20b", home, cwd).path, "/tmp/a b");
  EXPECT_EQ(parse_location("computer:///", home, cwd).kind, PlaceKind::Computer);
  EXPECT_EQ(parse_location("This PC", home, cwd).kind, PlaceKind::Computer);
  EXPECT_EQ(parse_location("network://", home, cwd).kind, PlaceKind::Network);
  EXPECT_EQ(parse_location("trash:///", home, cwd).kind, PlaceKind::Trash);
  EXPECT_EQ(parse_location("recent:///", home, cwd).kind, PlaceKind::Recent);
  EXPECT_EQ(parse_location("", home, cwd).kind, PlaceKind::Unknown);
  const Location smb = parse_location("\\\\nas\\media\\films", home, cwd);
  EXPECT_EQ(smb.kind, PlaceKind::Remote);
  EXPECT_EQ(smb.uri.scheme, "smb");
  EXPECT_EQ(smb.uri.host, "nas");
  EXPECT_EQ(smb.uri.path, "/media/films");
  EXPECT_EQ(parse_location("//nas/media", home, cwd).uri.host, "nas");
  const Location ftp = parse_location("ftp://ftp.example.com/pub", home, cwd);
  EXPECT_EQ(ftp.kind, PlaceKind::Remote);
  EXPECT_EQ(parse_location("gopher://x/", home, cwd).kind, PlaceKind::Unknown);
}

TEST(FmProtocols, TheListCoversWhatOtherFileManagersOffer) {
  for (const char* s : {"smb", "sftp", "ssh", "ftp", "ftps", "davs", "dav", "nfs", "afp", "mtp", "afc", "gphoto2", "http", "https", "archive", "admin"})
    EXPECT_NE(find_protocol(s), nullptr) << s;
  EXPECT_EQ(find_protocol("SFTP")->port, 22);
  EXPECT_EQ(find_protocol("smb")->port, 445);
  EXPECT_EQ(find_protocol("gopher"), nullptr);
  EXPECT_STREQ(find_protocol("ssh")->gvfs, "sftp");
  EXPECT_TRUE(find_protocol("davs")->encrypted);
  EXPECT_FALSE(find_protocol("ftp")->encrypted);
}

TEST(FmNextcloud, BuildsTheWebdavAddress) {
  EXPECT_EQ(nextcloud_webdav_uri("cloud.example.com", "anna"), "davs://anna@cloud.example.com/remote.php/dav/files/anna/");
  EXPECT_EQ(nextcloud_webdav_uri("https://cloud.example.com/", "anna"), "davs://anna@cloud.example.com/remote.php/dav/files/anna/");
  EXPECT_EQ(nextcloud_webdav_uri("https://example.com/nextcloud", "anna"), "davs://anna@example.com/nextcloud/remote.php/dav/files/anna/");
  EXPECT_EQ(nextcloud_webdav_uri("http://10.0.0.5", "bob"), "dav://bob@10.0.0.5/remote.php/dav/files/bob/");
  EXPECT_EQ(nextcloud_webdav_uri("https://c.example.com/remote.php/dav/files/anna/", "anna"), "davs://anna@c.example.com/remote.php/dav/files/anna/");
  EXPECT_EQ(nextcloud_webdav_uri("c.example.com", "an na@x"), "davs://an%20na%40x@c.example.com/remote.php/dav/files/an%20na%40x/");
  EXPECT_EQ(nextcloud_webdav_uri("", "anna"), "");
  EXPECT_EQ(nextcloud_webdav_uri("c.example.com", ""), "");
}

TEST(FmGvfs, MountFolderNamesMatchGvfsAndReadBackNicely) {
  EXPECT_EQ(gvfs_mount_dir_name(parse_uri("smb://nas/media/sub")), "smb-share:server=nas,share=media");
  EXPECT_EQ(gvfs_mount_dir_name(parse_uri("sftp://me@host/home")), "sftp:host=host,user=me");
  EXPECT_EQ(gvfs_mount_dir_name(parse_uri("ssh://host:2222/")), "sftp:host=host,port=2222");
  EXPECT_EQ(gvfs_mount_dir_name(parse_uri("ftp://ftp.example.com/pub")), "ftp:host=ftp.example.com");
  EXPECT_EQ(gvfs_mount_dir_name(parse_uri("davs://anna@c.example.com/remote.php/dav/files/anna/")),
            "dav:host=c.example.com,ssl=true,user=anna,prefix=%2Fremote.php%2Fdav%2Ffiles%2Fanna");
  EXPECT_EQ(gvfs_friendly_name("smb-share:server=nas,share=media"), "media (\\\\nas)");
  EXPECT_EQ(gvfs_friendly_name("sftp:host=host,user=me"), "me on host (SFTP)");
  EXPECT_EQ(gvfs_friendly_name("ftp:host=ftp.example.com"), "ftp.example.com (FTP)");
  EXPECT_EQ(gvfs_friendly_name("dav:host=c.example.com,ssl=true,user=anna,prefix=%2Fremote.php%2Fdav%2Ffiles%2Fanna"), "anna on c.example.com (Nextcloud)");
  EXPECT_EQ(gvfs_friendly_name("dav:host=h,ssl=true,prefix=%2Fwebdav"), "h (WebDAV, secure)");
  EXPECT_EQ(gvfs_friendly_name("mtp:host=Pixel_7"), "Pixel_7 (phone)");
  EXPECT_EQ(gvfs_friendly_name("something-else"), "something-else");
  EXPECT_EQ(gvfs_root(1000), "/run/user/1000/gvfs");
}

TEST(FmGvfs, KernelMountLabels) {
  EXPECT_EQ(network_mount_label("//nas/media", "cifs"), "media (\\\\nas)");
  EXPECT_EQ(network_mount_label("//nas/media/sub", "cifs"), "media\\sub (\\\\nas)");
  EXPECT_EQ(network_mount_label("nas:/srv/export", "nfs4"), "export (nas)");
  EXPECT_EQ(network_mount_label("me@host:/data", "fuse.sshfs"), "/data on host");
}

namespace {
class Fake : public CommandRunner {
 public:
  std::vector<std::vector<std::string>> calls;
  std::vector<std::string> inputs;
  RunResult next{0, "", false};
  std::function<void()> side_effect;
  RunResult run(const std::vector<std::string>& argv, const std::string& input, int) override {
    calls.push_back(argv);
    inputs.push_back(input);
    if (side_effect) side_effect();
    return next;
  }
};
}  // namespace

TEST(FmMountLocation, AnswersThePromptsAndReturnsTheFolder) {
  const fs::path gv = fs::temp_directory_path() / ("fm-gvfs-" + std::to_string(::getpid()));
  fs::create_directories(gv);
  Fake f;
  const Uri u = parse_uri("smb://nas/media");
  f.side_effect = [&] { fs::create_directories(gv / "smb-share:server=nas,share=media"); };
  Credentials c;
  c.user = "anna";
  c.password = "s3cret";
  c.domain = "HOME";
  const MountOutcome o = mount_location(u, c, f, gv.string());
  ASSERT_TRUE(o.ok) << o.error;
  EXPECT_EQ(o.path, (gv / "smb-share:server=nas,share=media").string());
  ASSERT_EQ(f.calls.size(), 1u);
  EXPECT_EQ(f.calls[0][0], "gio");
  EXPECT_EQ(f.calls[0][1], "mount");
  EXPECT_EQ(f.calls[0].back(), "smb://anna@nas/media");
  EXPECT_EQ(f.inputs[0], "anna\nHOME\ns3cret\n");
  // already mounted: no second call
  const MountOutcome again = mount_location(u, c, f, gv.string());
  EXPECT_TRUE(again.ok);
  EXPECT_EQ(f.calls.size(), 1u);
  fs::remove_all(gv);
}

TEST(FmMountLocation, FailureCarriesTheMessageAndNeverTheCommandLinePassword) {
  const fs::path gv = fs::temp_directory_path() / ("fm-gvfs2-" + std::to_string(::getpid()));
  fs::create_directories(gv);
  Fake f;
  f.next = {2, "Error mounting location: Failed to mount Windows share: Software caused connection abort\n", false};
  Credentials c;
  c.user = "anna";
  c.password = "s3cret";
  const MountOutcome o = mount_location(parse_uri("sftp://host/"), c, f, gv.string());
  EXPECT_FALSE(o.ok);
  EXPECT_NE(o.error.find("connection abort"), std::string::npos);
  for (const auto& a : f.calls[0]) EXPECT_EQ(a.find("s3cret"), std::string::npos);
  EXPECT_EQ(f.inputs[0], "s3cret\n");
  EXPECT_FALSE(mount_location(parse_uri("gopher://x/"), c, f, gv.string()).ok);
  Credentials anon;
  anon.anonymous = true;
  f.next = {0, "", false};
  f.side_effect = [&] { fs::create_directories(gv / "ftp:host=ftp.example.com"); };
  EXPECT_TRUE(mount_location(parse_uri("ftp://ftp.example.com/"), anon, f, gv.string()).ok);
  EXPECT_EQ(f.calls.back()[2], "--anonymous");
  EXPECT_EQ(f.inputs.back(), "");
  fs::remove_all(gv);
}

using FmPlaces = fleetwm::testutil::ScopedConfigHome;

TEST_F(FmPlaces, SavedPlacesRoundTripWithoutPasswords) {
  EXPECT_TRUE(load_places().empty());
  save_places({{"NAS media", "smb://anna:secret@nas/media"}, {"Cloud", "davs://anna@c.example.com/remote.php/dav/files/anna/"}});
  const auto p = load_places();
  ASSERT_EQ(p.size(), 2u);
  EXPECT_EQ(p[0].name, "NAS media");
  EXPECT_EQ(p[0].uri, "smb://anna@nas/media");
  EXPECT_EQ(p[1].uri, "davs://anna@c.example.com/remote.php/dav/files/anna/");
  std::ifstream f(places_path());
  std::string text((std::istreambuf_iterator<char>(f)), {});
  EXPECT_EQ(text.find("secret"), std::string::npos);
}

TEST_F(FmPlaces, BrokenFileGivesAnEmptyList) {
  write_config("fleetfm-places.toml", "place = [ {{{ nope");
  EXPECT_TRUE(load_places().empty());
}
