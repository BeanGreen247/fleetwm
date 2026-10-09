#include "mounts.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "dir_listing.hpp"
#include "locations.hpp"

namespace fs = std::filesystem;

namespace fleetwm::fm {

namespace {

std::string unescape_octal(std::string_view s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 3 < s.size() + 0 && s[i + 1] >= '0' && s[i + 1] <= '7' && s[i + 2] >= '0' && s[i + 2] <= '7' && s[i + 3] >= '0' && s[i + 3] <= '7') {
      out.push_back(static_cast<char>(((s[i + 1] - '0') << 6) | ((s[i + 2] - '0') << 3) | (s[i + 3] - '0')));
      i += 3;
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

std::string read_small(const fs::path& p) {
  std::ifstream f(p);
  std::string s;
  std::getline(f, s);
  while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\r')) s.pop_back();
  while (!s.empty() && s.front() == ' ') s.erase(s.begin());
  return s;
}

bool is_network_fs(const std::string& t) {
  static const std::set<std::string> net = {"cifs", "smb3", "smbfs", "nfs", "nfs4", "9p", "ceph", "glusterfs", "afs", "davfs",
                                            "fuse.sshfs", "fuse.rclone", "fuse.s3fs", "fuse.davfs2", "fuse.curlftpfs", "fuse.gvfsd-fuse"};
  return net.count(t) != 0;
}

bool is_virtual_fs(const std::string& t) {
  static const std::set<std::string> v = {"proc", "sysfs", "devtmpfs", "devpts", "tmpfs", "cgroup", "cgroup2", "pstore", "bpf",
                                          "securityfs", "debugfs", "tracefs", "configfs", "fusectl", "mqueue", "hugetlbfs",
                                          "autofs", "binfmt_misc", "efivarfs", "ramfs", "overlay", "squashfs", "rpc_pipefs",
                                          "nsfs", "fuse.portal", "fuse.gvfsd-fuse-skip", "securityfs", "selinuxfs"};
  return v.count(t) != 0;
}

std::string decode_label(std::string name) {  // udev writes "My\x20Disk"
  std::string out;
  for (size_t i = 0; i < name.size(); ++i) {
    if (name[i] == '\\' && i + 3 < name.size() + 0 && name[i + 1] == 'x') {
      out.push_back(static_cast<char>(std::strtol(name.substr(i + 2, 2).c_str(), nullptr, 16)));
      i += 3;
    } else {
      out.push_back(name[i]);
    }
  }
  return out;
}

std::string label_for(const std::string& dev_basename, const std::string& dev_root) {
  std::error_code ec;
  const fs::path dir = fs::path(dev_root) / "disk/by-label";
  for (const auto& e : fs::directory_iterator(dir, ec)) {
    std::error_code e2;
    if (fs::is_symlink(e.path(), e2) && fs::read_symlink(e.path(), e2).filename() == dev_basename) return decode_label(e.path().filename().string());
  }
  return {};
}

bool is_system_mount(const std::string& t) {
  static const char* sys[] = {"/", "/boot", "/boot/efi", "/usr", "/var", "/home", "/nix", "/etc", "/opt"};
  for (const char* s : sys)
    if (t == s) return true;
  return false;
}

bool hidden_mount(const MountEntry& m) {
  static const char* pre[] = {"/proc", "/sys", "/dev", "/run/snapd", "/snap", "/var/lib/docker", "/var/lib/snapd", "/run/credentials", "/boot/efi"};
  for (const char* p : pre) {
    const size_t n = std::strlen(p);
    if (m.target == p || m.target.compare(0, n + 1, std::string(p) + "/") == 0) return true;
  }
  return false;
}

}  // namespace

bool MountEntry::read_only() const {
  std::stringstream ss(options);
  std::string o;
  while (std::getline(ss, o, ','))
    if (o == "ro") return true;
  return false;
}

std::vector<MountEntry> parse_mountinfo(std::string_view text) {
  std::vector<MountEntry> out;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string_view::npos) eol = text.size();
    std::string_view line = text.substr(pos, eol - pos);
    pos = eol + 1;
    const size_t dash = line.find(" - ");
    if (dash == std::string_view::npos) continue;
    std::vector<std::string_view> head;
    for (size_t i = 0; i < dash;) {
      const size_t sp = line.find(' ', i);
      const size_t e = sp == std::string_view::npos || sp > dash ? dash : sp;
      head.push_back(line.substr(i, e - i));
      i = e + 1;
    }
    if (head.size() < 6) continue;
    std::vector<std::string_view> tail;
    for (size_t i = dash + 3; i < line.size();) {
      const size_t sp = line.find(' ', i);
      const size_t e = sp == std::string_view::npos ? line.size() : sp;
      tail.push_back(line.substr(i, e - i));
      i = e + 1;
    }
    if (tail.size() < 2) continue;
    MountEntry m;
    m.root = unescape_octal(head[3]);
    m.target = unescape_octal(head[4]);
    m.options = std::string(head[5]);
    m.fstype = std::string(tail[0]);
    m.source = unescape_octal(tail[1]);
    out.push_back(std::move(m));
  }
  return out;
}

bool read_block_info(const std::string& name, const std::string& sys_root, BlockInfo* out) {
  std::error_code ec;
  const fs::path link = fs::path(sys_root) / "class/block" / name;
  const fs::path real = fs::canonical(link, ec);
  if (ec) return false;
  const bool partition = fs::exists(real / "partition", ec);
  const fs::path disk_real = partition ? real.parent_path() : real;
  out->disk_name = disk_real.filename().string();
  out->removable = read_small(disk_real / "removable") == "1";
  out->usb = real.string().find("/usb") != std::string::npos;
  out->optical = out->disk_name.compare(0, 2, "sr") == 0 || out->disk_name.compare(0, 3, "scd") == 0;
  out->sd_card = out->disk_name.compare(0, 6, "mmcblk") == 0 && read_small(disk_real / "device/type") == "SD";
  out->model = read_small(disk_real / "device/model");
  return true;
}

DriveKind classify_volume(const MountEntry& m, const BlockInfo* b) {
  if (is_network_fs(m.fstype)) return DriveKind::Network;
  if (m.fstype == "iso9660" || (b && b->optical)) return DriveKind::Optical;
  if (b && (b->removable || b->usb || b->sd_card)) return DriveKind::Removable;
  if (m.source.compare(0, 5, "/dev/") == 0) return DriveKind::Internal;
  return DriveKind::Other;
}

std::string volume_display_name(const Volume& v) {
  const std::string base = v.device.empty() ? std::string() : v.device.substr(v.device.rfind('/') + 1);
  if (v.kind == DriveKind::Network) return v.label.empty() ? v.mountpoint : v.label;
  std::string kind;
  switch (v.kind) {
    case DriveKind::Removable: kind = "Removable Disk"; break;
    case DriveKind::Optical: kind = "DVD Drive"; break;
    default: kind = "Local Disk"; break;
  }
  const std::string name = v.label.empty() ? kind : v.label;
  if (v.mountpoint == "/") return name + " (/)";
  return name + (base.empty() ? "" : " (" + base + ")");
}

std::string volume_space_text(const Volume& v) {
  if (v.total == 0) return {};
  // Windows 10 words it "123 GB free of 465 GB"; the sizes drop the .00 of the properties format.
  auto sz = [](uint64_t b) {
    std::string s = [&] {
      static const char* u[] = {"bytes", "KB", "MB", "GB", "TB", "PB"};
      double d = static_cast<double>(b);
      int i = 0;
      while (d >= 1024.0 && i < 5) {
        d /= 1024.0;
        ++i;
      }
      char buf[32];
      std::snprintf(buf, sizeof buf, d < 10 && i > 0 ? "%.2f %s" : (d < 100 && i > 0 ? "%.1f %s" : "%.0f %s"), d, u[i]);
      return std::string(buf);
    }();
    return s;
  };
  return sz(v.free) + " free of " + sz(v.total);
}

void fill_space(Volume* v) {
  struct statvfs sv;
  if (v->mountpoint.empty() || ::statvfs(v->mountpoint.c_str(), &sv) != 0) return;
  v->total = static_cast<uint64_t>(sv.f_blocks) * sv.f_frsize;
  v->free = static_cast<uint64_t>(sv.f_bavail) * sv.f_frsize;
}

std::vector<Volume> list_volumes(const SysRoots& roots) {
  std::vector<Volume> out;
  std::string text;
  {
    std::ifstream f(fs::path(roots.proc) / "self/mountinfo");
    std::stringstream ss;
    ss << f.rdbuf();
    text = ss.str();
  }
  std::set<std::string> seen_devices;
  for (const MountEntry& m : parse_mountinfo(text)) {
    if (hidden_mount(m)) continue;
    if (m.fstype == "fuse.gvfsd-fuse") {
      // GVfs puts every mounted place (smb://, sftp://, ftp://, dav://, mtp://) in a sub folder here.
      DirListing l;
      if (list_dir(m.target, {false, true}, &l))
        for (const Entry& e : l.entries) {
          Volume v;
          v.mountpoint = m.target + "/" + std::string(l.name(e));
          v.label = gvfs_friendly_name(l.name(e));
          v.kind = DriveKind::Network;
          v.mounted = true;
          v.fstype = "gvfs";
          out.push_back(std::move(v));
        }
      continue;
    }
    if (is_virtual_fs(m.fstype) && !is_network_fs(m.fstype)) continue;
    const bool dev = m.source.compare(0, 5, "/dev/") == 0;
    if (!dev && !is_network_fs(m.fstype)) continue;
    std::string base = dev ? m.source.substr(5) : std::string();
    if (base.compare(0, 4, "loop") == 0 || base.compare(0, 3, "ram") == 0 || base.compare(0, 4, "zram") == 0) continue;
    if (dev && seen_devices.count(m.source) && m.root != "/") continue;
    BlockInfo bi;
    bool have = false;
    if (dev) {
      std::string name = base;
      std::error_code ec;
      if (base.compare(0, 7, "mapper/") == 0) {
        const fs::path real = fs::read_symlink(fs::path(roots.dev) / base, ec);
        if (!ec) name = real.filename().string();
      }
      have = read_block_info(name, roots.sys, &bi);
    }
    Volume v;
    v.device = dev ? m.source : std::string();
    v.mountpoint = m.target;
    v.fstype = m.fstype;
    v.mounted = true;
    v.read_only = m.read_only();
    v.kind = classify_volume(m, have ? &bi : nullptr);
    v.system = is_system_mount(m.target);
    if (have) {
      v.disk = "/dev/" + bi.disk_name;
      v.model = bi.model;
      v.ejectable = v.kind == DriveKind::Removable || v.kind == DriveKind::Optical;
    }
    if (dev) v.label = label_for(base.substr(base.rfind('/') + 1), roots.dev);
    if (v.kind == DriveKind::Network) {
      v.label = network_mount_label(m.source, m.fstype);
      v.ejectable = false;
    }
    seen_devices.insert(m.source);
    out.push_back(std::move(v));
  }
  // Partitions of removable disks that are plugged in but not mounted: they appear so they can be mounted with a click.
  std::error_code ec;
  std::set<std::string> mounted_dev;
  for (const Volume& v : out) mounted_dev.insert(v.device);
  for (const auto& d : fs::directory_iterator(fs::path(roots.sys) / "block", ec)) {
    const std::string disk = d.path().filename().string();
    BlockInfo bi;
    if (!read_block_info(disk, roots.sys, &bi) || !(bi.removable || bi.usb || bi.sd_card) || bi.optical) continue;
    const fs::path real = fs::canonical(fs::path(roots.sys) / "class/block" / disk, ec);
    if (ec || std::strtoull(read_small(real / "size").c_str(), nullptr, 10) == 0) continue;  // an empty card slot
    bool had_part = false;
    for (const auto& p : fs::directory_iterator(real, ec)) {
      if (!fs::exists(p.path() / "partition", ec)) continue;
      had_part = true;
      const std::string pn = p.path().filename().string();
      if (mounted_dev.count("/dev/" + pn)) continue;
      Volume v;
      v.device = "/dev/" + pn;
      v.disk = "/dev/" + disk;
      v.model = bi.model;
      v.kind = DriveKind::Removable;
      v.ejectable = true;
      v.label = label_for(pn, roots.dev);
      out.push_back(std::move(v));
    }
    if (!had_part && !mounted_dev.count("/dev/" + disk)) {
      Volume v;
      v.device = "/dev/" + disk;
      v.disk = v.device;
      v.model = bi.model;
      v.kind = DriveKind::Removable;
      v.ejectable = true;
      v.label = label_for(disk, roots.dev);
      out.push_back(std::move(v));
    }
  }
  for (Volume& v : out)
    if (v.mounted) fill_space(&v);
  std::stable_sort(out.begin(), out.end(), [](const Volume& a, const Volume& b) {
    auto rank = [](DriveKind k) { return k == DriveKind::Internal ? 0 : (k == DriveKind::Removable ? 1 : (k == DriveKind::Optical ? 2 : (k == DriveKind::Network ? 3 : 4))); };
    return rank(a.kind) < rank(b.kind);
  });
  return out;
}

std::vector<BusyUse> find_busy(const std::string& mp, const std::string& proc_root) {
  std::vector<BusyUse> out;
  if (mp.empty()) return out;
  auto inside = [&](const std::string& p) { return p == mp || p.compare(0, mp.size() + 1, mp + "/") == 0; };
  std::error_code ec;
  for (const auto& pd : fs::directory_iterator(proc_root, ec)) {
    const std::string name = pd.path().filename().string();
    if (name.empty() || name.find_first_not_of("0123456789") != std::string::npos) continue;
    int found = 0;
    BusyUse use;
    use.pid = std::atoi(name.c_str());
    auto check = [&](const fs::path& link) {
      std::error_code e;
      const fs::path t = fs::read_symlink(link, e);
      if (e || found >= 3) return;
      std::string s = t.string();
      const size_t del = s.rfind(" (deleted)");
      if (del != std::string::npos && del + 10 == s.size()) s.resize(del);
      if (inside(s)) {
        if (found++ == 0) use.path = s;
      }
    };
    check(pd.path() / "cwd");
    check(pd.path() / "exe");
    std::error_code e2;
    for (const auto& fd : fs::directory_iterator(pd.path() / "fd", e2)) check(fd.path());
    if (found) {
      use.comm = read_small(pd.path() / "comm");
      out.push_back(use);
    }
  }
  return out;
}

EjectResult eject_volume(const Volume& v, const std::vector<Volume>& all, CommandRunner& run, const EjectOptions& opt) {
  EjectResult r;
  const std::string name = volume_display_name(v);
  auto stage = [&](EjectStage s) {
    if (opt.on_stage) opt.on_stage(s);
  };
  auto problem = [&](const std::string& why) {
    r.ok = false;
    r.title = "Problem Ejecting " + name;
    r.message = why;
    return r;
  };
  if (v.system) return problem("This is part of the running system and cannot be removed.");
  const std::string using_text = "This device is currently in use. Close any programs, windows or terminals that might be using the device, and then try again.";

  if (v.mounted) {
    stage(EjectStage::Checking);
    r.users = find_busy(v.mountpoint, opt.proc_root);
    if (!r.users.empty()) {
      r.busy = true;
      return problem(using_text);
    }
    stage(EjectStage::Syncing);
    bool flushed;
    if (opt.flush) {
      flushed = opt.flush(v.mountpoint);
    } else {
      const int fd = ::open(v.mountpoint.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
      flushed = fd >= 0 && ::syncfs(fd) == 0;
      if (fd >= 0) ::close(fd);
      if (v.kind == DriveKind::Network) flushed = true;  // best effort: the server owns the data
    }
    if (!flushed) return problem("Writing the cached data to the device failed. Do not remove it; check the device and try again.");

    stage(EjectStage::Unmounting);
    RunResult ur;
    if (v.fstype == "gvfs") ur = run.run({"gio", "mount", "-u", v.mountpoint});
    else if (v.fstype.compare(0, 4, "fuse") == 0) ur = run.run({"fusermount", "-u", v.mountpoint});
    else if (v.kind == DriveKind::Network || v.device.empty()) ur = run.run({"umount", v.mountpoint});
    else ur = run.run({"udisksctl", "unmount", "-b", v.device, "--no-user-interaction"});
    if (ur.status != 0 && !v.device.empty() && v.kind != DriveKind::Network) {
      if (ur.output.find("busy") == std::string::npos) ur = run.run({"umount", v.mountpoint});
    }
    if (ur.status != 0) {
      if (ur.output.find("busy") != std::string::npos) {
        r.busy = true;
        r.users = find_busy(v.mountpoint, opt.proc_root);
        return problem(using_text);
      }
      std::string why = ur.output;
      while (!why.empty() && (why.back() == '\n' || why.back() == ' ')) why.pop_back();
      return problem(why.empty() ? "The volume could not be unmounted." : why);
    }
  }

  r.ok = true;
  if (opt.power_off && v.ejectable && !v.disk.empty()) {
    stage(EjectStage::PoweringOff);
    bool others = false;
    for (const Volume& o : all)
      if (o.mounted && o.disk == v.disk && o.device != v.device) others = true;
    if (!others) {
      RunResult pr = v.kind == DriveKind::Optical ? run.run({"eject", v.device}) : run.run({"udisksctl", "power-off", "-b", v.disk, "--no-user-interaction"});
      r.powered_off = pr.status == 0;
    }
  }
  stage(EjectStage::Done);
  r.title = "Safe to Remove Hardware";
  if (r.powered_off || !v.ejectable) {
    r.message = "The '" + name + "' device can now be safely removed from the computer.";
  } else {
    r.message = "'" + name + "' was unmounted. " + (v.ejectable ? "Other parts of the same device are still in use; remove it after ejecting them." : "");
  }
  return r;
}

bool mount_volume(const Volume& v, CommandRunner& run, std::string* mountpoint, std::string* error) {
  if (v.device.empty()) {
    if (error) *error = "Nothing to mount";
    return false;
  }
  const RunResult r = run.run({"udisksctl", "mount", "-b", v.device, "--no-user-interaction"});
  if (r.status != 0) {
    if (error) *error = r.output;
    return false;
  }
  const size_t at = r.output.rfind(" at ");
  if (at != std::string::npos && mountpoint) {
    std::string mp = r.output.substr(at + 4);
    while (!mp.empty() && (mp.back() == '\n' || mp.back() == '.' || mp.back() == ' ')) mp.pop_back();
    *mountpoint = mp;
  }
  return true;
}

}  // namespace fleetwm::fm
