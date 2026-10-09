#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fleetwm::fm {

// A file name the user typed (rename, new folder): not empty, no '/', not "." or "..", at most 255 bytes, no NUL.
bool valid_file_name(const std::string& name, std::string* why = nullptr);
// "New folder", "New folder (2)" ... free in `dir`.
std::string new_folder_name(const std::string& dir, const std::string& base = "New folder");
bool make_dir(const std::string& path, std::string* err = nullptr);
// Renames inside one folder; refuses to overwrite.
bool rename_in_place(const std::string& path, const std::string& new_name, std::string* err = nullptr);
// Permanent delete, folders included. Never follows symlinks.
bool delete_tree(const std::string& path, std::string* err = nullptr);

struct TreeSize {
  uint64_t bytes = 0, on_disk = 0, files = 0, folders = 0;
};
// Totals below a path (a file counts as one); stops early when *cancel turns true.
TreeSize measure_tree(const std::string& path, const std::atomic<bool>* cancel = nullptr);

struct FileProps {
  std::string path, name, type, link_target, owner, group, mode_text;  // mode_text: "-rw-r--r--"
  uint32_t mode = 0;
  uint64_t size = 0, on_disk = 0;
  int64_t modified = 0, accessed = 0, changed = 0;
  bool is_dir = false, is_link = false;
};
bool read_props(const std::string& path, FileProps* out);
// "drwxr-xr-x" from st_mode.
std::string mode_string(uint32_t mode);

// A symbolic link to `target` inside `dir`, named "name - Link" (then "name - Link (2)" ...); `made` receives the new path.
bool make_symlink_to(const std::string& target, const std::string& dir, std::string* made, std::string* err = nullptr);
// A hard link to the file `target` inside `dir` ("name - Link" too). Fails for folders and across file systems, with the system's reason.
bool make_hardlink_to(const std::string& target, const std::string& dir, std::string* made, std::string* err = nullptr);

// Starts `argv` detached (own session, no zombie, no shell). false when it could not be started.
bool spawn_detached(const std::vector<std::string>& argv);
// xdg-open on a path or address.
bool open_default(const std::string& path_or_uri);

// The mime type of a file by its extension ("photo.jpg" -> image/jpeg, a folder -> inode/directory); application/octet-stream when unknown.
std::string mime_type_for(std::string_view name, bool is_dir);
// True when `program` is found in $PATH.
bool have_program(const std::string& program);

// Paths from clipboard text: lines of file:// URIs or absolute paths (what other file managers put there).
std::vector<std::string> parse_path_list(const std::string& text);
// The text form of a path list: one file:// URI per line.
std::string make_uri_list(const std::vector<std::string>& paths);

}  // namespace fleetwm::fm

namespace fleetwm::fm {

struct RecentFile {
  std::string path;
  int64_t modified = 0;  // seconds since the epoch
};
// Reads the freedesktop recently-used.xbel (GTK, Nemo, Caja, Nautilus and Thunar all write it): newest first, only files that
// still exist, at most `limit`.
std::vector<RecentFile> read_recent(const std::string& xbel_path, size_t limit = 100);

}  // namespace fleetwm::fm
