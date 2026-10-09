#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace fleetwm::fm {

// Explorer's name order: digit runs compare as numbers ("file2" < "file10"), letters ignore case (ASCII fold; other bytes
// compare as they are, so UTF-8 sorts by code point). Returns <0, 0, >0.
int natural_compare(std::string_view a, std::string_view b);

// A string whose plain byte order is natural_compare's order, except that names natural_compare ranks equal by case or leading zeros
// get the same key (the caller breaks that tie with natural_compare). Sorting 100k names by memcmp of these keys is several times
// cheaper than calling natural_compare on every comparison. Appended to `out`; returns the key length.
size_t append_natural_key(std::string_view name, std::string* out);

}  // namespace fleetwm::fm
