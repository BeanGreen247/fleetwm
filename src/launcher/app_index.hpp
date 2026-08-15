#pragma once

#include <gio/gio.h>

#include <string>
#include <vector>

namespace fleetwm::launcher {

struct AppEntry {
  std::string name;
  std::string secondary_text;  // Comment=, used for search but not a label
  std::string category_hint;   // human-readable, e.g. "Web Browser"
  GDesktopAppInfo* info;       // owned by AppIndex, valid for its lifetime
};

// Enumerates installed applications via GLib's GDesktopAppInfo -- no
// hand-rolled .desktop parsing. Built once at startup; entries are stable
// for the process's short lifetime (spawned fresh per launcher invocation).
class AppIndex {
 public:
  AppIndex();
  ~AppIndex();

  AppIndex(const AppIndex&) = delete;
  AppIndex& operator=(const AppIndex&) = delete;

  const std::vector<AppEntry>& entries() const { return entries_; }

  // Case-insensitive substring match against name + secondary_text,
  // sorted by earliest match position then alphabetically. Empty query
  // returns all entries in alphabetical order.
  std::vector<const AppEntry*> search(const std::string& query) const;

 private:
  std::vector<AppEntry> entries_;
};

}  // namespace fleetwm::launcher
