#include "format.hpp"

#include <cmath>
#include <cstdio>

namespace fleetwm::fm {

std::string group_digits(uint64_t n) {
  std::string s = std::to_string(n), out;
  const int len = static_cast<int>(s.size());
  for (int i = 0; i < len; ++i) {
    out.push_back(s[i]);
    const int left = len - 1 - i;
    if (left > 0 && left % 3 == 0) out.push_back(',');
  }
  return out;
}

std::string format_size_kb(uint64_t bytes) { return group_digits((bytes + 1023) / 1024) + " KB"; }

std::string format_size(uint64_t bytes) {
  if (bytes == 1) return "1 byte";
  if (bytes < 1024) return std::to_string(bytes) + " bytes";
  static const char* units[] = {"KB", "MB", "GB", "TB", "PB", "EB"};
  double v = static_cast<double>(bytes);
  int u = -1;
  while (v >= 1024.0 && u < 5) {
    v /= 1024.0;
    ++u;
  }
  // Three significant figures: 1.23, 12.3, 123.
  const int decimals = v < 10 ? 2 : (v < 100 ? 1 : 0);
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.*f %s", decimals, v, units[u]);
  return buf;
}

std::string format_size_exact(uint64_t bytes) {
  if (bytes < 1024) return format_size(bytes);
  return format_size(bytes) + " (" + group_digits(bytes) + " bytes)";
}

std::string format_speed(double bps) {
  if (!(bps > 0)) return "0 KB/s";
  std::string s = format_size(static_cast<uint64_t>(bps));
  if (s.size() > 6 && s.compare(s.size() - 6, 6, " bytes") == 0) s = std::to_string(static_cast<uint64_t>(bps)) + " B";
  return s + "/s";
}

std::string format_remaining(double s) {
  if (s < 0 || !std::isfinite(s)) return "Calculating...";
  const long total = static_cast<long>(std::ceil(s));
  if (total < 60) return "About " + std::to_string(total < 1 ? 1 : total) + (total <= 1 ? " second" : " seconds");
  const long mins = (total + 30) / 60;
  if (mins < 60) return "About " + std::to_string(mins) + (mins == 1 ? " minute" : " minutes");
  const long h = total / 3600, m = (total % 3600 + 30) / 60;
  std::string out = "About " + std::to_string(h) + (h == 1 ? " hour" : " hours");
  if (m > 0) out += " " + std::to_string(m) + (m == 1 ? " minute" : " minutes");
  return out;
}

std::string format_date(time_t t, DateStyle style) {
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[48];
  switch (style) {
    case DateStyle::Iso:
      std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
      break;
    case DateStyle::European:
      std::strftime(buf, sizeof buf, "%d.%m.%Y %H:%M", &tm);
      break;
    case DateStyle::Windows7: {
      const int h12 = tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12;
      std::snprintf(buf, sizeof buf, "%d/%d/%d %d:%02d %s", tm.tm_mon + 1, tm.tm_mday, tm.tm_year + 1900, h12, tm.tm_min,
                    tm.tm_hour < 12 ? "AM" : "PM");
      break;
    }
  }
  return buf;
}

}  // namespace fleetwm::fm
