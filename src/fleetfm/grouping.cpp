#include "grouping.hpp"

namespace fleetwm::fm {

namespace {
time_t midnight(time_t t, int days_back) {
  std::tm tm{};
  localtime_r(&t, &tm);
  tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
  tm.tm_mday -= days_back;
  tm.tm_isdst = -1;
  return mktime(&tm);
}
}  // namespace

GroupKey group_of(GroupBy by, std::string_view name, bool is_dir, uint64_t size, int64_t mtime, time_t now, const std::string& type_label) {
  switch (by) {
    case GroupBy::Name: {
      if (name.empty()) return {1000, "#"};
      const unsigned char c = name[0];
      if (c >= '0' && c <= '9') return {0, "0-9"};
      if (c >= 'a' && c <= 'z') return {1 + (c - 'a'), std::string(1, static_cast<char>(c - 32))};
      if (c >= 'A' && c <= 'Z') return {1 + (c - 'A'), std::string(1, static_cast<char>(c))};
      return {1000, "#"};  // punctuation, other alphabets
    }
    case GroupBy::Type: {
      std::string label = is_dir ? "File folder" : type_label;
      if (label.empty()) label = "File";
      // folders first, then alphabetically: the rank is the first letters of the label
      int rank = is_dir ? 0 : 1000 + static_cast<unsigned char>(label[0]) * 256 + (label.size() > 1 ? static_cast<unsigned char>(label[1]) : 0);
      return {rank, label};
    }
    case GroupBy::Size: {
      if (is_dir) return {0, "Folders"};
      if (size == 0) return {1, "Empty (0 KB)"};
      if (size < (16u << 10)) return {2, "Tiny (0 - 16 KB)"};
      if (size < (1u << 20)) return {3, "Small (16 KB - 1 MB)"};
      if (size < (128u << 20)) return {4, "Medium (1 - 128 MB)"};
      if (size < (1ull << 30)) return {5, "Large (128 MB - 1 GB)"};
      return {6, "Huge (over 1 GB)"};
    }
    case GroupBy::Modified: {
      const time_t today = midnight(now, 0), yesterday = midnight(now, 1);
      std::tm tm{};
      localtime_r(&now, &tm);
      const time_t week = midnight(now, (tm.tm_wday + 6) % 7);  // Monday
      const time_t last_week = midnight(week, 7);
      std::tm first{};
      localtime_r(&now, &first);
      first.tm_mday = 1;
      first.tm_hour = first.tm_min = first.tm_sec = 0;
      first.tm_isdst = -1;
      const time_t month = mktime(&first);
      std::tm lm = first;
      lm.tm_mon -= 1;
      lm.tm_isdst = -1;
      const time_t last_month = mktime(&lm);
      std::tm fy = first;
      fy.tm_mon = 0;
      fy.tm_isdst = -1;
      const time_t year = mktime(&fy);
      if (mtime >= today) return {0, "Today"};
      if (mtime >= yesterday) return {1, "Yesterday"};
      if (mtime >= week) return {2, "Earlier this week"};
      if (mtime >= last_week) return {3, "Last week"};
      if (mtime >= month) return {4, "Earlier this month"};
      if (mtime >= last_month) return {5, "Last month"};
      if (mtime >= year) return {6, "Earlier this year"};
      return {7, "A long time ago"};
    }
    case GroupBy::None: break;
  }
  return {};
}

}  // namespace fleetwm::fm
