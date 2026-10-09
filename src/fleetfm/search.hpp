#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "dir_listing.hpp"

// Search below a folder, the way Explorer's search box does: type, and matching names appear. "*" and "?" are wildcards;
// without them the text matches anywhere in the name (partial match). Runs on a worker thread and checks `cancel` between
// entries, so typing another letter stops the old search at once.

namespace fleetwm::fm {

struct SearchOptions {
  std::string query;
  bool subfolders = true;
  bool partial = true;         // "rep" finds "Report.doc"; off, the whole name must match
  bool hidden = false;
  bool contents = false;       // also look inside text files (up to 4 MiB each, binary files skipped)
  bool case_sensitive = false;
  size_t max_results = 5000;
};

struct SearchHit {
  std::string path;            // relative to the search root
  Kind kind = Kind::File;
  uint64_t size = 0;
  int64_t mtime = 0;
};

// Name test only (no contents).
bool name_matches(const SearchOptions& o, std::string_view name);
// Walks `root`; calls on_hit for every match (return false to stop). Returns the number of hits.
size_t search_tree(const std::string& root, const SearchOptions& o, const std::atomic<bool>* cancel, const std::function<bool(const SearchHit&)>& on_hit);

}  // namespace fleetwm::fm
