#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The freedesktop.org Trash specification (the same one Nautilus, Nemo, Caja, Thunar and Dolphin use), so files deleted here
// show up in every other file manager's trash and the other way round. Files on the home file system go to
// $XDG_DATA_HOME/Trash; files on another drive go to <drive>/.Trash-<uid> (a rename, not a copy, so deleting from a USB
// stick is instant and the file is still on the stick). A move that cannot be a rename is refused: the caller asks whether
// to delete permanently instead.

namespace fleetwm::fm {

struct TrashItem {
  std::string name;           // the name in <trash>/files
  std::string original_path;  // where it came from
  std::string deleted;        // "2026-10-09T06:40:00" (local time)
  std::string trash_dir;      // the Trash folder it lives in
  uint64_t size = 0;
  bool is_dir = false;
};

class Trash {
 public:
  // `home_trash` defaults to $XDG_DATA_HOME/Trash (or ~/.local/share/Trash).
  explicit Trash(std::string home_trash = {});
  const std::string& home_trash() const { return home_; }
  // Moves `path` to the right trash. false with *err when it cannot be done by renaming.
  // `where` (optional) receives the item as it now sits in the trash, so the move can be undone.
  bool put(const std::string& path, std::string* err = nullptr, TrashItem* where = nullptr);
  // Everything in the home trash and in the per-drive trashes of the given mount points.
  std::vector<TrashItem> list(const std::vector<std::string>& mount_points = {}) const;
  bool restore(const TrashItem& item, std::string* err = nullptr);
  bool remove(const TrashItem& item, std::string* err = nullptr);  // delete for good
  // Empties the home trash and the per-drive trashes; returns how many items went.
  size_t empty(const std::vector<std::string>& mount_points = {});

 private:
  std::string trash_dir_for(const std::string& path) const;
  std::string home_;
};

// Path value as it is written in a .trashinfo file (percent-encoded, '/' kept).
std::string trashinfo_path_encode(const std::string& p);
std::string trashinfo_path_decode(const std::string& p);

}  // namespace fleetwm::fm
