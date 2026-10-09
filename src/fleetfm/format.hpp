#pragma once

#include <cstdint>
#include <ctime>
#include <string>

// Text the way Windows Explorer words it, shared by the file list, the status bar and the transfer window.

namespace fleetwm::fm {

// Details-column size: whole KB rounded up, like Explorer ("0 KB" for an empty file, "13 KB" for 12,345 bytes).
std::string format_size_kb(uint64_t bytes);
// Properties-style size: "1.23 MB", "512 bytes", "1 byte" (three significant figures, 1024 steps).
std::string format_size(uint64_t bytes);
// "1.23 MB (1,289,748 bytes)".
std::string format_size_exact(uint64_t bytes);
// "12.3 MB/s".
std::string format_speed(double bytes_per_second);
// Transfer estimate: "About 5 seconds", "About 2 minutes", "About 1 hour 5 minutes"; "Calculating..." when unknown (<0).
std::string format_remaining(double seconds);
// "1,234,567".
std::string group_digits(uint64_t n);

enum class DateStyle { Iso, Windows7, European };
// Local time. Iso "2026-10-09 04:19", Windows7 "10/9/2026 4:19 AM", European "09.10.2026 04:19".
std::string format_date(time_t t, DateStyle style = DateStyle::Windows7);

}  // namespace fleetwm::fm
