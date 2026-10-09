#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include <functional>

#include "dir_listing.hpp"
#include "fm_settings.hpp"
#include "locations.hpp"
#include "view_style.hpp"

// One tab: where it is, how it got there, what it shows and what is selected. No drawing and no I/O beyond what the caller
// feeds in, so every rule (history, selection, filtering, sorting, type-to-select) is unit-tested.

namespace fleetwm::fm {

struct Browser {
  uint64_t id = 0;                      // stable while the tab lives (indexes move when tabs close)
  // ---- where ----
  PlaceKind place = PlaceKind::Local;
  std::string path = "/";               // Local: the folder. Remote: the mounted folder.
  std::string uri;                      // Remote: the address it came from (shown in the address bar)
  std::string search;                   // non-empty: showing search results below `path`
  std::vector<std::string> back, forward;  // history, as typed addresses ("/home/me", "computer:///")

  // ---- what ----
  DirListing raw;                       // as read from disk
  std::vector<Entry> shown;             // after hidden/filter, sorted
  std::string filter;                   // the search box's live text for the current folder
  std::string error;
  bool loading = false;
  uint64_t generation = 0;              // bumped per load; a stale result is ignored

  // ---- how ----
  GroupBy group_by = GroupBy::None;
  std::vector<int> group_starts;        // where each group begins in `shown` (filled by rebuild when grouping)
  std::vector<std::string> group_labels;
  std::function<std::string(std::string_view name, bool is_dir)> type_label;  // names a file's type, for grouping by type
  ViewMode mode = ViewMode::Details;
  SortKey sort_key = SortKey::Name;
  bool ascending = true, folders_first = true, show_hidden = false;
  int scroll_x = 0, scroll_y = 0;

  // ---- selection ----
  std::vector<char> sel;                // parallel to `shown`
  int focus = -1, anchor = -1;
  std::string typed;                    // type-to-select buffer
  double typed_at = 0;

  // history
  // The address string for this tab ("/home/me", "computer:///", "smb://nas/x").
  std::string address() const;
  void remember_current(size_t keep = 200);  // push the current address on `back` (clears `forward`); keeps at most `keep` entries
  bool can_back() const { return !back.empty(); }
  bool can_forward() const { return !forward.empty(); }
  bool can_up() const;
  std::string up_address() const;       // parent folder address ("" at the top)
  // Moves through history, returning the address to open (the caller loads it); "" when there is none.
  std::string go_back();
  std::string go_forward();

  // data
  // Installs a freshly read folder, keeping the selection of names that still exist.
  void set_listing(DirListing&& l);
  // Re-applies hidden files, the filter and the sort to `raw`.
  void rebuild();
  std::string name_at(int i) const { return std::string(raw.name(shown[i])); }
  std::string path_at(int i) const;     // full path of shown[i] (names that start with '/' are already full paths: Recent)
  std::string label_at(int i) const;    // what the Name column shows: the last component of the name
  std::string folder_at(int i) const;   // the folder part of a search or Recent result ("" for a plain listing)
  bool is_dir_at(int i) const { return raw.is_dir(shown[i]); }

  // selection
  int selected_count() const;
  std::vector<int> selected() const;
  uint64_t selected_bytes(bool* complete = nullptr) const;  // complete = false when a selected item has not been stat'ed yet
  // Fills size and date of shown[i] (and of its source entry) if the listing was read without them. Cheap, cached.
  void ensure_stat(int i);
  void ensure_stat_range(int first, int last);
  bool stat_complete = true;            // every entry of `raw` has its size and date
  bool stat_pending = false;            // a background pass is filling them in
  void clear_selection();
  void select_only(int i);              // click
  void toggle(int i);                   // ctrl+click
  void select_to(int i);                // shift+click: anchor..i
  void select_all();
  void invert_selection();
  void select_indices(const std::vector<int>& idx, bool add);  // rubber band
  void set_focus(int i);                // moves the focus without touching the selection
  // Type-to-select: appends the letter (resets after 1 s), selects the first match; returns the index or -1.
  int type_ahead(const std::string& letter, double now);
  // Called after a delete / reload when indexes shifted.
  void fix_focus();
};

}  // namespace fleetwm::fm
