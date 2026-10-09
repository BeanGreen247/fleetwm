#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include "mounts.hpp"

using namespace fleetwm::fm;
namespace fs = std::filesystem;

namespace {

// Records what was asked and answers from a table keyed by the first two words.
class FakeRunner : public CommandRunner {
 public:
  std::vector<std::vector<std::string>> calls;
  std::map<std::string, RunResult> answers;
  RunResult run(const std::vector<std::string>& argv, const std::string&, int) override {
    calls.push_back(argv);
    const std::string key = argv[0] + (argv.size() > 1 ? " " + argv[1] : "");
    auto it = answers.find(key);
    return it == answers.end() ? RunResult{0, "", false} : it->second;
  }
  bool called(const std::string& key) const {
    for (const auto& c : calls)
      if (c[0] + (c.size() > 1 ? " " + c[1] : "") == key) return true;
    return false;
  }
};

class FmMounts : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ = fs::temp_directory_path() / ("fm-mounts-" + std::to_string(::getpid()) + "-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
    fs::create_directories(root_);
  }
  void TearDown() override {
    std::error_code ec;
    fs::remove_all(root_, ec);
  }
  void put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
  }
  // A disk in sysfs: devices/<bus>/block/<name>, linked from class/block and block.
  void fake_disk(const std::string& name, const std::string& bus, bool removable, const std::string& model, uint64_t sectors, const std::vector<std::string>& parts) {
    const fs::path d = root_ / "sys/devices" / bus / "block" / name;
    put(d / "removable", removable ? "1\n" : "0\n");
    put(d / "size", std::to_string(sectors) + "\n");
    put(d / "device/model", model + "   \n");
    fs::create_directories(root_ / "sys/class/block");
    fs::create_directories(root_ / "sys/block");
    fs::create_directory_symlink(d, root_ / "sys/class/block" / name);
    fs::create_directory_symlink(d, root_ / "sys/block" / name);
    for (const std::string& p : parts) {
      put(d / p / "partition", "1\n");
      fs::create_directory_symlink(d / p, root_ / "sys/class/block" / p);
    }
  }
  SysRoots roots() const { return {(root_ / "sys").string(), (root_ / "dev").string(), (root_ / "proc").string()}; }
  fs::path root_;
};

const char* kMountinfo =
    "22 1 8:1 / / rw,relatime shared:1 - ext4 /dev/sda1 rw,errors=remount-ro\n"
    "23 22 0:6 / /dev rw,nosuid shared:2 - devtmpfs udev rw\n"
    "24 22 0:21 / /proc rw,nosuid shared:3 - proc proc rw\n"
    "30 22 8:17 / /media/me/My\\040Stick rw,nosuid,nodev,relatime shared:9 - vfat /dev/sdb1 rw,uid=1000\n"
    "31 22 8:33 / /mnt/usb2 ro,relatime - ext4 /dev/sdc1 ro\n"
    "32 22 0:50 / /mnt/nas rw,relatime - cifs //nas/media rw,vers=3.0\n"
    "33 22 0:51 / /mnt/ex rw,relatime - nfs4 nas:/srv/export rw\n"
    "34 22 7:0 / /snap/core/1 ro - squashfs /dev/loop0 ro\n"
    "35 22 8:2 / /boot/efi rw - vfat /dev/sda2 rw\n";

}  // namespace

TEST(FmMountinfo, ParsesFieldsAndOctalEscapes) {
  const auto m = parse_mountinfo(kMountinfo);
  ASSERT_EQ(m.size(), 9u);
  EXPECT_EQ(m[0].source, "/dev/sda1");
  EXPECT_EQ(m[0].target, "/");
  EXPECT_EQ(m[0].fstype, "ext4");
  EXPECT_EQ(m[3].target, "/media/me/My Stick");
  EXPECT_TRUE(m[4].read_only());
  EXPECT_FALSE(m[3].read_only());
  EXPECT_EQ(m[5].source, "//nas/media");
}

TEST(FmMountinfo, SkipsBrokenLines) {
  EXPECT_TRUE(parse_mountinfo("garbage\n\nno dash here 1 2 3 4 5 6\n").empty());
}

TEST_F(FmMounts, BlockInfoSeesRemovableUsbDisks) {
  fake_disk("sdb", "pci0000:00/0000:00:14.0/usb1/1-1", true, "Flash Drive", 1000000, {"sdb1"});
  BlockInfo b;
  ASSERT_TRUE(read_block_info("sdb1", (root_ / "sys").string(), &b));
  EXPECT_EQ(b.disk_name, "sdb");
  EXPECT_TRUE(b.removable);
  EXPECT_TRUE(b.usb);
  EXPECT_FALSE(b.optical);
  EXPECT_EQ(b.model, "Flash Drive");
  EXPECT_FALSE(read_block_info("nope", (root_ / "sys").string(), &b));
}

TEST_F(FmMounts, ClassifiesByFilesystemAndBus) {
  fake_disk("sda", "pci0000:00/0000:00:17.0/ata1/host0/target0:0:0/0:0:0:0", false, "SSD", 1000000, {"sda1"});
  fake_disk("sr0", "pci0000:00/0000:00:17.0/ata2/host1", true, "DVD", 1000000, {});
  BlockInfo internal, optical;
  ASSERT_TRUE(read_block_info("sda1", (root_ / "sys").string(), &internal));
  ASSERT_TRUE(read_block_info("sr0", (root_ / "sys").string(), &optical));
  EXPECT_EQ(classify_volume({"/dev/sda1", "/", "ext4", "", "/"}, &internal), DriveKind::Internal);
  EXPECT_EQ(classify_volume({"/dev/sr0", "/media/dvd", "udf", "", "/"}, &optical), DriveKind::Optical);
  EXPECT_EQ(classify_volume({"x", "/m", "iso9660", "", "/"}, nullptr), DriveKind::Optical);
  EXPECT_EQ(classify_volume({"//nas/x", "/m", "cifs", "", "/"}, nullptr), DriveKind::Network);
  EXPECT_EQ(classify_volume({"h:/x", "/m", "nfs4", "", "/"}, nullptr), DriveKind::Network);
  EXPECT_EQ(classify_volume({"u@h:/", "/m", "fuse.sshfs", "", "/"}, nullptr), DriveKind::Network);
  EXPECT_EQ(classify_volume({"tmpfs", "/t", "tmpfs", "", "/"}, nullptr), DriveKind::Other);
}

TEST_F(FmMounts, ListsDrivesLikeThisPc) {
  fake_disk("sda", "pci0000:00/0000:00:17.0/ata1/host0/target0:0:0/0:0:0:0", false, "SSD", 1000000, {"sda1", "sda2"});
  fake_disk("sdb", "pci0000:00/0000:00:14.0/usb1/1-1", true, "Flash Drive", 1000000, {"sdb1"});
  fake_disk("sdc", "pci0000:00/0000:00:14.0/usb1/1-2", true, "Backup Disk", 2000000, {"sdc1", "sdc2"});
  fake_disk("sdd", "pci0000:00/0000:00:14.0/usb1/1-3", true, "Card Reader", 0, {});  // an empty slot
  put(root_ / "proc/self/mountinfo", kMountinfo);
  fs::create_directories(root_ / "dev/disk/by-label");
  fs::create_directory_symlink("../../sdb1", root_ / "dev/disk/by-label/My\\x20Stick");
  fs::create_directory_symlink("../../sdc2", root_ / "dev/disk/by-label/Photos");
  const auto vols = list_volumes(roots());
  std::map<std::string, Volume> by;
  for (const Volume& v : vols) by[v.device.empty() ? v.mountpoint : v.device] = v;
  ASSERT_TRUE(by.count("/dev/sda1"));
  EXPECT_EQ(by["/dev/sda1"].kind, DriveKind::Internal);
  EXPECT_TRUE(by["/dev/sda1"].system);
  EXPECT_FALSE(by["/dev/sda1"].ejectable);
  EXPECT_FALSE(by.count("/dev/sda2")) << "the EFI partition stays hidden";
  EXPECT_FALSE(by.count("/dev/loop0"));
  ASSERT_TRUE(by.count("/dev/sdb1"));
  EXPECT_EQ(by["/dev/sdb1"].kind, DriveKind::Removable);
  EXPECT_TRUE(by["/dev/sdb1"].ejectable);
  EXPECT_EQ(by["/dev/sdb1"].mountpoint, "/media/me/My Stick");
  EXPECT_EQ(by["/dev/sdb1"].label, "My Stick");
  EXPECT_EQ(by["/dev/sdb1"].disk, "/dev/sdb");
  EXPECT_TRUE(by["/dev/sdc1"].read_only);
  EXPECT_FALSE(by["/dev/sdc1"].read_only && false);
  // sdc2 is plugged in, not mounted: offered so it can be mounted
  ASSERT_TRUE(by.count("/dev/sdc2"));
  EXPECT_FALSE(by["/dev/sdc2"].mounted);
  EXPECT_EQ(by["/dev/sdc2"].label, "Photos");
  EXPECT_FALSE(by.count("/dev/sdd")) << "an empty card slot is not a drive";
  ASSERT_TRUE(by.count("/mnt/nas"));
  EXPECT_EQ(by["/mnt/nas"].kind, DriveKind::Network);
  EXPECT_EQ(by["/mnt/nas"].label, "media (\\\\nas)");
  EXPECT_EQ(by["/mnt/ex"].label, "export (nas)");
  // sorted: internal, removable, network
  size_t first_net = vols.size(), last_internal = 0, first_rem = vols.size();
  for (size_t i = 0; i < vols.size(); ++i) {
    if (vols[i].kind == DriveKind::Network) first_net = std::min(first_net, i);
    if (vols[i].kind == DriveKind::Internal) last_internal = i;
    if (vols[i].kind == DriveKind::Removable) first_rem = std::min(first_rem, i);
  }
  EXPECT_LT(last_internal, first_rem);
  EXPECT_LT(first_rem, first_net);
}

TEST(FmVolumeText, NamesAndSpaceLikeWindows) {
  Volume v;
  v.device = "/dev/sdb1";
  v.kind = DriveKind::Removable;
  EXPECT_EQ(volume_display_name(v), "Removable Disk (sdb1)");
  v.label = "Backup";
  EXPECT_EQ(volume_display_name(v), "Backup (sdb1)");
  Volume root;
  root.device = "/dev/sda1";
  root.mountpoint = "/";
  root.kind = DriveKind::Internal;
  EXPECT_EQ(volume_display_name(root), "Local Disk (/)");
  Volume dvd;
  dvd.device = "/dev/sr0";
  dvd.kind = DriveKind::Optical;
  EXPECT_EQ(volume_display_name(dvd), "DVD Drive (sr0)");
  Volume net;
  net.kind = DriveKind::Network;
  net.label = "media (\\\\nas)";
  EXPECT_EQ(volume_display_name(net), "media (\\\\nas)");
  v.total = 500ull << 30;
  v.free = 123ull << 30;
  EXPECT_EQ(volume_space_text(v), "123 GB free of 500 GB");
  EXPECT_NEAR(v.used_fraction(), 1.0 - 123.0 / 500.0, 1e-9);
  v.total = 0;
  EXPECT_EQ(volume_space_text(v), "");
  EXPECT_EQ(v.used_fraction(), 0.0);
}

TEST_F(FmMounts, FindsProgramsHoldingTheVolume) {
  const fs::path mp = root_ / "media/stick";
  fs::create_directories(mp / "dir");
  auto proc = [&](int pid, const std::string& comm, const fs::path& cwd, const std::vector<fs::path>& fds) {
    const fs::path p = root_ / "proc" / std::to_string(pid);
    put(p / "comm", comm + "\n");
    fs::create_directory_symlink(cwd, p / "cwd");
    fs::create_directories(p / "fd");
    int n = 3;
    for (const auto& f : fds) fs::create_symlink(f, p / "fd" / std::to_string(n++));
  };
  proc(100, "bash", mp / "dir", {});
  proc(200, "vlc", "/home/me", {mp / "movie.mkv"});
  proc(300, "editor", "/home/me", {"/home/me/x", mp.string() + "2/other"});  // "stick2" is another folder
  proc(400, "gone", "/home/me", {mp / "deleted.txt (deleted)"});
  const auto busy = find_busy(mp.string(), (root_ / "proc").string());
  std::set<int> pids;
  for (const auto& b : busy) pids.insert(b.pid);
  EXPECT_EQ(pids, (std::set<int>{100, 200, 400}));
  for (const auto& b : busy)
    if (b.pid == 200) EXPECT_EQ(b.comm, "vlc");
  EXPECT_TRUE(find_busy("", (root_ / "proc").string()).empty());
}

namespace {
Volume stick() {
  Volume v;
  v.device = "/dev/sdb1";
  v.disk = "/dev/sdb";
  v.mountpoint = "/media/me/Stick";
  v.label = "Stick";
  v.fstype = "vfat";
  v.kind = DriveKind::Removable;
  v.mounted = true;
  v.ejectable = true;
  return v;
}
}  // namespace

TEST_F(FmMounts, EjectFlushesUnmountsThenPowersOffInThatOrder) {
  FakeRunner r;
  std::vector<std::string> order;
  EjectOptions o;
  o.proc_root = (root_ / "proc").string();
  fs::create_directories(o.proc_root);
  o.flush = [&](const std::string& mp) {
    order.push_back("flush " + mp);
    return true;
  };
  o.on_stage = [&](EjectStage s) { order.push_back("stage " + std::to_string(static_cast<int>(s))); };
  const Volume v = stick();
  const EjectResult res = eject_volume(v, {v}, r, o);
  EXPECT_TRUE(res.ok);
  EXPECT_TRUE(res.powered_off);
  EXPECT_EQ(res.title, "Safe to Remove Hardware");
  EXPECT_EQ(res.message, "The 'Stick (sdb1)' device can now be safely removed from the computer.");
  ASSERT_EQ(r.calls.size(), 2u);
  EXPECT_EQ(r.calls[0][1], "unmount");
  EXPECT_EQ(r.calls[0][3], "/dev/sdb1");
  EXPECT_EQ(r.calls[1][1], "power-off");
  EXPECT_EQ(r.calls[1][3], "/dev/sdb");
  ASSERT_GE(order.size(), 3u);
  EXPECT_EQ(order[0], "stage 0");
  EXPECT_EQ(order[1], "stage 1");
  EXPECT_EQ(order[2], "flush /media/me/Stick");
}

TEST_F(FmMounts, EjectRefusesWhileAProgramHoldsTheDriveAndTouchesNothing) {
  FakeRunner r;
  EjectOptions o;
  o.proc_root = (root_ / "proc").string();
  Volume v = stick();
  v.mountpoint = (root_ / "m").string();
  fs::create_directories(v.mountpoint);
  const fs::path p = root_ / "proc/55";
  put(p / "comm", "vlc\n");
  fs::create_directory_symlink(root_, p / "cwd");  // the temp root contains the mount point, not the other way round
  fs::create_directories(p / "fd");
  fs::create_symlink(fs::path(v.mountpoint) / "a.mkv", p / "fd/3");
  o.flush = [](const std::string&) { ADD_FAILURE() << "must not flush a busy volume"; return true; };
  const EjectResult res = eject_volume(v, {v}, r, o);
  EXPECT_FALSE(res.ok);
  EXPECT_TRUE(res.busy);
  ASSERT_EQ(res.users.size(), 1u);
  EXPECT_EQ(res.users[0].comm, "vlc");
  EXPECT_NE(res.message.find("currently in use"), std::string::npos);
  EXPECT_TRUE(r.calls.empty());
}

TEST_F(FmMounts, EjectStopsWhenTheFlushFails) {
  FakeRunner r;
  EjectOptions o;
  o.proc_root = (root_ / "proc").string();
  fs::create_directories(o.proc_root);
  o.flush = [](const std::string&) { return false; };
  const Volume v = stick();
  const EjectResult res = eject_volume(v, {v}, r, o);
  EXPECT_FALSE(res.ok);
  EXPECT_TRUE(r.calls.empty());
  EXPECT_NE(res.message.find("Do not remove"), std::string::npos);
}

TEST_F(FmMounts, EjectReportsBusyFromTheUnmountItself) {
  FakeRunner r;
  r.answers["udisksctl unmount"] = {1, "Error unmounting /dev/sdb1: target is busy\n", false};
  r.answers["umount /media/me/Stick"] = {32, "umount: target is busy.\n", false};
  EjectOptions o;
  o.proc_root = (root_ / "proc").string();
  fs::create_directories(o.proc_root);
  o.flush = [](const std::string&) { return true; };
  const Volume v = stick();
  const EjectResult res = eject_volume(v, {v}, r, o);
  EXPECT_FALSE(res.ok);
  EXPECT_TRUE(res.busy);
  EXPECT_FALSE(r.called("udisksctl power-off"));
}

TEST_F(FmMounts, EjectLeavesThePowerOnWhileAnotherPartitionIsMounted) {
  FakeRunner r;
  EjectOptions o;
  o.proc_root = (root_ / "proc").string();
  fs::create_directories(o.proc_root);
  o.flush = [](const std::string&) { return true; };
  Volume a = stick(), b = stick();
  b.device = "/dev/sdb2";
  b.mountpoint = "/media/me/Other";
  const EjectResult res = eject_volume(a, {a, b}, r, o);
  EXPECT_TRUE(res.ok);
  EXPECT_FALSE(res.powered_off);
  EXPECT_FALSE(r.called("udisksctl power-off"));
  EXPECT_NE(res.message.find("still in use"), std::string::npos);
}

TEST_F(FmMounts, EjectNeverTouchesTheSystemDrive) {
  FakeRunner r;
  Volume v;
  v.device = "/dev/sda1";
  v.mountpoint = "/";
  v.mounted = true;
  v.system = true;
  v.kind = DriveKind::Internal;
  const EjectResult res = eject_volume(v, {v}, r);
  EXPECT_FALSE(res.ok);
  EXPECT_TRUE(r.calls.empty());
}

TEST_F(FmMounts, EjectUsesTheRightToolForNetworkPlaces) {
  FakeRunner r;
  EjectOptions o;
  o.proc_root = (root_ / "proc").string();
  fs::create_directories(o.proc_root);
  o.flush = [](const std::string&) { return true; };
  Volume g;
  g.mountpoint = "/run/user/1000/gvfs/smb-share:server=nas,share=media";
  g.fstype = "gvfs";
  g.kind = DriveKind::Network;
  g.mounted = true;
  EXPECT_TRUE(eject_volume(g, {g}, r, o).ok);
  EXPECT_EQ(r.calls.back()[0], "gio");
  Volume c;
  c.mountpoint = "/mnt/nas";
  c.fstype = "cifs";
  c.kind = DriveKind::Network;
  c.mounted = true;
  EXPECT_TRUE(eject_volume(c, {c}, r, o).ok);
  EXPECT_EQ(r.calls.back()[0], "umount");
  EXPECT_FALSE(r.called("udisksctl power-off"));
}

TEST_F(FmMounts, OpticalDrivesAreEjectedWithEject) {
  FakeRunner r;
  EjectOptions o;
  o.proc_root = (root_ / "proc").string();
  fs::create_directories(o.proc_root);
  o.flush = [](const std::string&) { return true; };
  Volume d;
  d.device = "/dev/sr0";
  d.disk = "/dev/sr0";
  d.mountpoint = "/media/me/DISC";
  d.fstype = "iso9660";
  d.kind = DriveKind::Optical;
  d.mounted = true;
  d.ejectable = true;
  EXPECT_TRUE(eject_volume(d, {d}, r, o).powered_off);
  EXPECT_EQ(r.calls.back()[0], "eject");
}

TEST(FmMountVolume, ParsesWhereUdisksPutIt) {
  FakeRunner r;
  r.answers["udisksctl mount"] = {0, "Mounted /dev/sdb1 at /run/media/me/My Stick.\n", false};
  Volume v;
  v.device = "/dev/sdb1";
  std::string mp, err;
  EXPECT_TRUE(mount_volume(v, r, &mp, &err));
  EXPECT_EQ(mp, "/run/media/me/My Stick");
  r.answers["udisksctl mount"] = {1, "Error mounting: not authorized\n", false};
  EXPECT_FALSE(mount_volume(v, r, &mp, &err));
  EXPECT_NE(err.find("not authorized"), std::string::npos);
  Volume none;
  EXPECT_FALSE(mount_volume(none, r, &mp, &err));
}
