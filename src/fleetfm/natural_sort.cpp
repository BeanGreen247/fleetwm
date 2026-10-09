#include "natural_sort.hpp"

namespace fleetwm::fm {

namespace {
inline bool is_digit(unsigned char c) { return c >= '0' && c <= '9'; }
inline unsigned char fold(unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
}  // namespace

int natural_compare(std::string_view a, std::string_view b) {
  size_t i = 0, j = 0;
  int tie = 0;  // decided by case, then by leading zeros, only if everything else is equal
  while (i < a.size() && j < b.size()) {
    const unsigned char ca = a[i], cb = b[j];
    if (is_digit(ca) && is_digit(cb)) {
      size_t ei = i, ej = j;
      while (ei < a.size() && is_digit(a[ei])) ++ei;
      while (ej < b.size() && is_digit(b[ej])) ++ej;
      size_t zi = i, zj = j;
      while (zi < ei - 1 && a[zi] == '0') ++zi;
      while (zj < ej - 1 && b[zj] == '0') ++zj;
      const size_t li = ei - zi, lj = ej - zj;
      if (li != lj) return li < lj ? -1 : 1;
      for (size_t k = 0; k < li; ++k)
        if (a[zi + k] != b[zj + k]) return a[zi + k] < b[zj + k] ? -1 : 1;
      if (!tie && (zi - i) != (zj - j)) tie = (zi - i) > (zj - j) ? 1 : -1;
      i = ei;
      j = ej;
      continue;
    }
    const unsigned char fa = fold(ca), fb = fold(cb);
    if (fa != fb) return fa < fb ? -1 : 1;
    if (!tie && ca != cb) tie = ca < cb ? 1 : -1;  // lower case before upper case on a tie
    ++i;
    ++j;
  }
  if (i < a.size()) return 1;
  if (j < b.size()) return -1;
  return tie;
}

size_t append_natural_key(std::string_view n, std::string* out) {
  const size_t start = out->size();
  size_t i = 0;
  while (i < n.size()) {
    const unsigned char c = n[i];
    if (is_digit(c)) {
      size_t e = i;
      while (e < n.size() && is_digit(n[e])) ++e;
      size_t z = i;
      while (z < e - 1 && n[z] == '0') ++z;  // leading zeros do not count
      const size_t len = e - z;
      // '0' sorts after every character below the digits and before every one above them; the length then orders the numbers
      out->push_back('0');
      out->push_back(static_cast<char>(len >> 8));
      out->push_back(static_cast<char>(len & 255));
      out->append(n.substr(z, len));
      i = e;
    } else {
      out->push_back(static_cast<char>(fold(c)));
      ++i;
    }
  }
  return out->size() - start;
}

}  // namespace fleetwm::fm
