#pragma once

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "fm_settings.hpp"
#include "icons.hpp"
#include "locations.hpp"
#include "mounts.hpp"

// The navigation pane as a flat list of rows: Windows 7's tree (Favorites, Libraries, Computer with its drives, Network)
// or the flat "Places / Devices / Network" sidebar the Linux file managers and Finder have. Pure data: the window draws the
// rows and feeds clicks back through toggle().

namespace fleetwm::fm {

struct NavRow {
  bool header = false;           // a section title, not clickable (Sidebar) or a clickable group (Tree: Computer, Network)
  int depth = 0;
  std::string label, sub;        // sub: "123 GB free of 465 GB"
  IconKind icon = IconKind::Folder;
  std::string address;           // what to open; empty for a plain title
  std::string key;               // expansion key
  bool expandable = false, expanded = false;
  bool eject = false;            // draw the eject button
  bool has_volume = false;
  Volume volume;
  double used_fraction = 0;      // for the space bar (volumes)
};

struct NavInput {
  const FmSettings* settings = nullptr;
  ViewStyle style = ViewStyle::Windows7;
  std::vector<Volume> volumes;
  std::vector<SavedPlace> places;
  std::string home;
  std::string current;           // the open address: its branch is expanded when nav_expand_to_current is on
  // Subfolders of a folder (names only, no hidden); the window passes a real lister, tests a fake.
  std::function<std::vector<std::string>(const std::string&)> subfolders;
};

class NavTree {
 public:
  void build(const NavInput& in);
  const std::vector<NavRow>& rows() const { return rows_; }
  // Expands or collapses the row with this key; the caller rebuilds. false when it is not expandable.
  bool toggle(const std::string& key);
  bool is_expanded(const std::string& key) const { return expanded_.count(key) != 0; }
  int row_for_address(const std::string& address) const;

 private:
  void add_children(const NavInput& in, int depth, const std::string& path);
  void add_item(NavRow r);
  std::vector<NavRow> rows_;
  std::set<std::string> expanded_;
  bool first_build_ = true;
};

}  // namespace fleetwm::fm
