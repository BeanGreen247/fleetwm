#pragma once

#include <cairo.h>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

// Every icon the file manager shows is drawn here with cairo paths at the size asked for: the application icon, the file
// type icons, drives and places. No image files are read, so a folder full of files costs no disk access for icons, and the
// result is sharp at any size. Rendered icons are kept in a small cache keyed by kind and size.

namespace fleetwm::fm {

enum class IconKind : uint8_t {
  App,
  Folder, FolderOpen, File, Text, Document, Spreadsheet, Presentation, Pdf, Image, Audio, Video, Archive, Code, Executable, Font, Disc,
  DriveInternal, DriveUsb, DriveOptical, DriveNetwork, DriveCard,
  Computer, Network, NetworkServer, Cloud, Home, Desktop, Documents, Downloads, Music, Pictures, Videos, Trash, Recent, Star, Library,
  Count
};

// What icon a directory entry gets: by type, then extension ("photo.JPG" -> Image, "Makefile" -> Code ...).
IconKind icon_for_file(std::string_view name, bool is_dir);
// Well-known folder names under the home folder ("Downloads" -> Downloads ...); Folder otherwise.
IconKind icon_for_folder_name(std::string_view name);
const char* icon_kind_name(IconKind k);
// "Text Document", "JPEG image", "Folder", "Application" -- Explorer's Type column.
std::string type_description(std::string_view name, bool is_dir);

// Draws into the square (0,0)-(size,size) of `cr`.
void draw_icon(cairo_t* cr, IconKind kind, double size);
// Small overlays in a corner of an icon: a shortcut arrow and a padlock.
void draw_link_badge(cairo_t* cr, double size);
void draw_lock_badge(cairo_t* cr, double size);
// The application icon is also written as a PNG for the desktop entry (`fleetwm-fm --write-icon out.png 256`).
bool write_icon_png(IconKind kind, int size, const std::string& path);

class IconCache {
 public:
  explicit IconCache(size_t max_entries = 256) : max_(max_entries) {}
  ~IconCache();
  IconCache(const IconCache&) = delete;
  IconCache& operator=(const IconCache&) = delete;
  // The icon as a surface (owned by the cache, valid until the next get() that evicts it or clear()).
  cairo_surface_t* get(IconKind kind, int size);
  size_t size() const { return map_.size(); }
  uint64_t hits() const { return hits_; }
  uint64_t misses() const { return misses_; }
  void clear();

 private:
  struct Slot {
    cairo_surface_t* surface;
    uint64_t used;
  };
  std::map<uint32_t, Slot> map_;
  size_t max_;
  uint64_t tick_ = 0, hits_ = 0, misses_ = 0;
};

}  // namespace fleetwm::fm
