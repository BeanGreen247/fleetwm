#pragma once

#include <ctime>
#include <string>
#include <string_view>

#include "dir_listing.hpp"

namespace fleetwm::fm {

// "Group by": which group an entry belongs to. `rank` orders the groups (ascending is the natural order: A before B, small before big, today
// before last year) and `label` is the heading. `type_label` names a file's type for GroupBy::Type.
struct GroupKey {
  int rank = 0;
  std::string label;
};
GroupKey group_of(GroupBy by, std::string_view name, bool is_dir, uint64_t size, int64_t mtime, time_t now, const std::string& type_label);

}  // namespace fleetwm::fm
