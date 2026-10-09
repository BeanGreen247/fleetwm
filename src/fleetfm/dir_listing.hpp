#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// A folder read with raw getdents64 into one name arena and a flat entry array: one syscall per ~100s of names, no
// per-entry allocation, sorting moves 40-byte records and never the names. The point is the open-a-big-folder latency.

namespace fleetwm::fm {

enum class Kind : uint8_t { File, Dir, Symlink, Other };

struct Entry {
  uint32_t name_off = 0, name_len = 0;  // into DirListing::arena
  uint32_t src = 0;          // index in DirListing::entries of the listing this entry was copied from (a filtered, sorted view keeps it)
  Kind kind = Kind::File;
  bool hidden = false;       // name starts with '.'
  bool link_to_dir = false;  // a symlink whose target is a folder: opens like one
  bool has_stat = false;     // size, mtime and mode are filled in (lazy listings leave them until a row is drawn)
  uint32_t mode = 0;         // st_mode
  uint64_t size = 0;
  int64_t mtime = 0;         // seconds
};

struct DirListing {
  std::vector<Entry> entries;
  std::string arena;
  std::string_view name(const Entry& e) const { return std::string_view(arena).substr(e.name_off, e.name_len); }
  bool is_dir(const Entry& e) const { return e.kind == Kind::Dir || e.link_to_dir; }
};

struct ListOptions {
  bool stat = true;           // size, mtime and mode (statx per entry); off = names and kinds only (d_type), the rest on demand
  bool show_hidden = true;    // keep dot files in the result
};

// Reads `path`. Returns false with *err set when it cannot be opened. Entries that vanish mid-read are skipped.
bool list_dir(const std::string& path, const ListOptions& opt, DirListing* out, std::string* err = nullptr);
// Fills size, mtime, mode and the link target kind of one entry of a folder read without stat. False when it vanished.
bool stat_entry(const std::string& dir, const std::string& name, Entry* e);

enum class SortKey { Name, Size, Modified, Type };
enum class GroupBy { None, Name, Type, Size, Modified };
// Stable sort; folders first when asked (Explorer default). Type compares the extension, then the name.
void sort_listing(DirListing* l, SortKey key, bool ascending = true, bool folders_first = true);
// The same over any subset of a listing's entries (a filtered view): names come from `names`' arena.
void sort_entries(const DirListing& names, std::vector<Entry>* entries, SortKey key, bool ascending = true, bool folders_first = true);
int find_prefix(const DirListing& names, const std::vector<Entry>& entries, std::string_view prefix, int from = 0);

// Index of the first entry whose name starts with `prefix` (case-insensitive), or -1: type-to-select.
int find_prefix(const DirListing& l, std::string_view prefix, int from = 0);

// Extension without the dot, lower-case ("" for none or a leading-dot name like ".bashrc").
std::string extension_of(std::string_view name);

}  // namespace fleetwm::fm
