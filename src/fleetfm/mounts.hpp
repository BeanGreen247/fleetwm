#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "process.hpp"

// Drives and mounted places: what "This PC" lists, and the safe way to take a removable drive away. Parsing and
// classification are pure functions over text (unit-tested with fixtures); the readers take their /sys, /proc and /dev
// roots as arguments so a test can build a fake tree.

namespace fleetwm::fm {

enum class DriveKind { Internal, Removable, Optical, Network, Other };

struct MountEntry {
  std::string source, target, fstype, options, root;
  bool read_only() const;
};
// One line per mount, /proc/self/mountinfo format (octal escapes like \040 are decoded).
std::vector<MountEntry> parse_mountinfo(std::string_view text);

struct SysRoots {
  std::string sys = "/sys", dev = "/dev", proc = "/proc";
};

struct Volume {
  std::string device;       // /dev/sdb1, or empty for a network place
  std::string disk;         // /dev/sdb: the whole disk the partition lives on
  std::string mountpoint;   // empty when not mounted
  std::string label, fstype, model;
  DriveKind kind = DriveKind::Other;
  bool mounted = false, read_only = false;
  bool ejectable = false;   // a USB / SD / optical drive that can be switched off after unmounting
  bool system = false;      // holds the running system (/, /boot ...): never unmounted from here
  uint64_t total = 0, free = 0;
  double used_fraction() const { return total ? 1.0 - static_cast<double>(free) / static_cast<double>(total) : 0.0; }
};

// "Removable Disk (sdb1)", "Backup (sdb1)", "Local Disk (/)", "DVD Drive (sr0)", "share (\\nas)".
std::string volume_display_name(const Volume& v);
// "123 GB free of 465 GB" (Windows 10 tile text); empty when the size is unknown.
std::string volume_space_text(const Volume& v);

struct BlockInfo {
  std::string disk_name;    // sdb
  bool removable = false, usb = false, optical = false, sd_card = false;
  std::string model;
};
// Looks a block device up in sysfs (e.g. "sdb1" -> disk sdb, removable flag, USB bus). Unknown device: found == false.
bool read_block_info(const std::string& dev_basename, const std::string& sys_root, BlockInfo* out);
DriveKind classify_volume(const MountEntry& m, const BlockInfo* block);

// Everything mounted that is worth showing, plus unmounted partitions of removable disks, sorted: internal, removable,
// optical, network.
std::vector<Volume> list_volumes(const SysRoots& roots = {});
// Fills total/free from statvfs (a mounted volume only).
void fill_space(Volume* v);

// Programs holding something open below `mountpoint` (open files, working directory, mapped files).
struct BusyUse {
  int pid = 0;
  std::string comm, path;
};
std::vector<BusyUse> find_busy(const std::string& mountpoint, const std::string& proc_root = "/proc");

enum class EjectStage { Checking, Syncing, Unmounting, PoweringOff, Done };
struct EjectResult {
  bool ok = false;
  bool powered_off = false;
  bool busy = false;
  std::string title, message;  // worded like Windows: "Safe to Remove Hardware" / "Problem Ejecting ..."
  std::vector<BusyUse> users;
};
struct EjectOptions {
  std::string proc_root = "/proc";
  bool power_off = true;
  std::function<void(EjectStage)> on_stage;
  // Flushes the volume's cached writes to the device; default: syncfs() on the mount point. Tests replace it.
  std::function<bool(const std::string& mountpoint)> flush;
};
// Check for programs using it -> flush -> unmount -> power off the disk when it is removable and nothing else on it is
// mounted. Never forces: a busy volume is reported with the programs that hold it.
EjectResult eject_volume(const Volume& v, const std::vector<Volume>& all, CommandRunner& run, const EjectOptions& opt = {});

// Mount an unmounted partition through udisks (no root needed). `mountpoint` receives where it went.
bool mount_volume(const Volume& v, CommandRunner& run, std::string* mountpoint, std::string* error);

}  // namespace fleetwm::fm
