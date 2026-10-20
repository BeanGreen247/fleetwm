#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "backend.hpp"

// The text formats FTP and WebDAV servers answer with. Pure functions so the odd servers can be pinned down by tests.

namespace fleetwm::netfs {

// MLSD (RFC 3659): "type=file;size=12;modify=20240102030405; name". cdir/pdir lines are skipped.
std::vector<NetEntry> parse_mlsd(std::string_view text);
// The Unix "ls -l" style LIST output almost every FTP server speaks. `now` decides whether a date without a year is this year or last.
std::vector<NetEntry> parse_unix_list(std::string_view text, int64_t now);
// PROPFIND Depth:1 multistatus. `dir_path` is the requested folder (percent-decoded, no trailing slash); the response for the folder
// itself is left out. Works with any namespace prefix (d:, D:, lp1:, none).
std::vector<NetEntry> parse_propfind(std::string_view xml, const std::string& dir_path);
// A single response (Depth:0), for stat.
bool parse_propfind_one(std::string_view xml, NetStat* st);

std::string url_decode(std::string_view s);
std::string url_encode_path(std::string_view s);  // keeps '/'
int64_t parse_http_date(std::string_view s);      // "Sun, 06 Nov 1994 08:49:37 GMT" or ISO 8601; 0 when unparsable

}  // namespace fleetwm::netfs
