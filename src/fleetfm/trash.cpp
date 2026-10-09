#include "trash.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "locations.hpp"
#include "transfer.hpp"

namespace fs = std::filesystem;

namespace fleetwm::fm {

namespace {
std::string default_home_trash() {
  const char* xdg = std::getenv("XDG_DATA_HOME");
  if (xdg && *xdg) return std::string(xdg) + "/Trash";
  const char* home = std::getenv("HOME");
  return std::string(home ? home : "") + "/.local/share/Trash";
}

dev_t dev_of(const std::string& p) {
  struct stat st;
  return ::lstat(p.c_str(), &st) == 0 ? st.st_dev : static_cast<dev_t>(-1);
}

// The mount point holding `path`: walk up while the device stays the same.
std::string topdir_of(const std::string& path) {
  fs::path p = fs::absolute(path).lexically_normal();
  if (p.has_filename() == false) p = p.parent_path();
  const dev_t d = dev_of(p.parent_path().string());
  fs::path cur = p.parent_path();
  while (cur.has_parent_path() && cur != cur.parent_path()) {
    if (dev_of(cur.parent_path().string()) != d) break;
    cur = cur.parent_path();
  }
  return cur.string();
}

std::string now_iso() {
  const time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &tm);
  return buf;
}

uint64_t tree_size(const fs::path& p) {
  std::error_code ec;
  if (fs::is_symlink(p, ec) || !fs::is_directory(p, ec)) return fs::is_symlink(p, ec) ? 0 : fs::file_size(p, ec);
  uint64_t n = 0;
  for (auto it = fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    std::error_code e2;
    if (it->is_regular_file(e2)) n += it->file_size(e2);
  }
  return n;
}

bool ensure_dirs(const std::string& trash, std::string* err) {
  std::error_code ec;
  fs::create_directories(trash + "/files", ec);
  fs::create_directories(trash + "/info", ec);
  if (ec) {
    if (err) *err = ec.message();
    return false;
  }
  ::chmod(trash.c_str(), 0700);
  return true;
}
}  // namespace

std::string trashinfo_path_encode(const std::string& p) { return percent_encode(p, true); }
std::string trashinfo_path_decode(const std::string& p) { return percent_decode(p); }

Trash::Trash(std::string home_trash) : home_(home_trash.empty() ? default_home_trash() : std::move(home_trash)) {}

std::string Trash::trash_dir_for(const std::string& path) const {
  std::error_code ec;
  fs::create_directories(home_, ec);
  const std::string parent = fs::path(fs::absolute(path)).parent_path().string();
  if (dev_of(parent) == dev_of(home_)) return home_;
  return topdir_of(path) + "/.Trash-" + std::to_string(::getuid());
}

bool Trash::put(const std::string& path, std::string* err, TrashItem* where) {
  struct stat st;
  if (::lstat(path.c_str(), &st) != 0) {
    if (err) *err = std::strerror(errno);
    return false;
  }
  const std::string abs = fs::absolute(path).lexically_normal().string();
  const std::string dir = trash_dir_for(abs);
  if (!ensure_dirs(dir, err)) return false;
  // Never trash the trash itself.
  if (abs == dir || abs.compare(0, dir.size() + 1, dir + "/") == 0) {
    if (err) *err = "That is the Trash";
    return false;
  }
  const std::string base = fs::path(abs).filename().string();
  const std::string name = unique_name(dir + "/files", base);
  // The info file is created first, exclusively: it reserves the name even if two programs trash at once.
  std::string info = dir + "/info/" + name + ".trashinfo";
  const int fd = ::open(info.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (fd < 0) {
    if (err) *err = std::strerror(errno);
    return false;
  }
  // Paths on a per-drive trash are stored relative to that drive's top directory.
  std::string stored = abs;
  if (dir != home_) {
    const std::string top = fs::path(dir).parent_path().string();
    stored = abs.compare(0, top.size() + 1, top + "/") == 0 ? abs.substr(top.size() + 1) : abs;
  }
  const std::string body = "[Trash Info]\nPath=" + trashinfo_path_encode(stored) + "\nDeletionDate=" + now_iso() + "\n";
  const bool wrote = ::write(fd, body.data(), body.size()) == static_cast<ssize_t>(body.size());
  ::close(fd);
  if (!wrote || ::rename(abs.c_str(), (dir + "/files/" + name).c_str()) != 0) {
    const int e = errno;
    ::unlink(info.c_str());
    if (err) *err = !wrote ? "Writing the trash information failed" : std::strerror(e);
    return false;
  }
  if (where) {
    where->name = name;
    where->original_path = abs;
    where->trash_dir = dir;
    where->is_dir = S_ISDIR(st.st_mode);
    where->size = static_cast<uint64_t>(st.st_size);
  }
  return true;
}

std::vector<TrashItem> Trash::list(const std::vector<std::string>& mounts) const {
  std::vector<std::string> dirs = {home_};
  for (const std::string& m : mounts) {
    const std::string d = m + "/.Trash-" + std::to_string(::getuid());
    std::error_code ec;
    if (fs::is_directory(d, ec)) dirs.push_back(d);
  }
  std::vector<TrashItem> out;
  for (const std::string& dir : dirs) {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir + "/info", ec)) {
      if (e.path().extension() != ".trashinfo") continue;
      TrashItem it;
      it.trash_dir = dir;
      it.name = e.path().stem().string();
      std::ifstream f(e.path());
      std::string line, path;
      while (std::getline(f, line)) {
        if (line.compare(0, 5, "Path=") == 0) path = trashinfo_path_decode(line.substr(5));
        else if (line.compare(0, 13, "DeletionDate=") == 0) it.deleted = line.substr(13);
      }
      if (path.empty()) continue;
      if (path[0] != '/' && dir != home_) path = fs::path(dir).parent_path().string() + "/" + path;
      it.original_path = path;
      const fs::path stored = fs::path(dir) / "files" / it.name;
      std::error_code e2;
      if (!fs::exists(fs::symlink_status(stored, e2))) continue;
      it.is_dir = fs::is_directory(stored, e2) && !fs::is_symlink(stored, e2);
      it.size = tree_size(stored);
      out.push_back(std::move(it));
    }
  }
  return out;
}

bool Trash::restore(const TrashItem& item, std::string* err) {
  const fs::path from = fs::path(item.trash_dir) / "files" / item.name;
  std::error_code ec;
  if (fs::exists(fs::symlink_status(item.original_path, ec))) {
    if (err) *err = "Something with that name already exists at the original location";
    return false;
  }
  fs::create_directories(fs::path(item.original_path).parent_path(), ec);
  if (::rename(from.c_str(), item.original_path.c_str()) != 0) {
    if (err) *err = std::strerror(errno);
    return false;
  }
  ::unlink((fs::path(item.trash_dir) / "info" / (item.name + ".trashinfo")).c_str());
  return true;
}

bool Trash::remove(const TrashItem& item, std::string* err) {
  std::error_code ec;
  fs::remove_all(fs::path(item.trash_dir) / "files" / item.name, ec);
  if (ec) {
    if (err) *err = ec.message();
    return false;
  }
  fs::remove(fs::path(item.trash_dir) / "info" / (item.name + ".trashinfo"), ec);
  return true;
}

size_t Trash::empty(const std::vector<std::string>& mounts) {
  size_t n = 0;
  for (const TrashItem& it : list(mounts))
    if (remove(it)) ++n;
  return n;
}

}  // namespace fleetwm::fm
