// shared canonical dump of a parsed toml document; works with toml++ and minitoml
#include <cstdio>
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <cmath>
static std::string esc(const std::string& s) {
  std::string o; for (unsigned char c : s) { if (c=='"'||c=='\\') { o+='\\'; o+=c; } else if (c<0x20) { char b[8]; std::snprintf(b,8,"\\x%02x",c); o+=b; } else o+=c; } return o; }
template <class N> void dump(const N& n, std::ostream& o) {
  if (auto* t = n.as_table()) {
    o << "{"; bool first = true;
    // sorted by key for both libraries
    std::vector<std::pair<std::string, const N*>> items;
    for (auto&& [k, v] : *t) items.push_back({std::string(k.str()), &v});
    std::sort(items.begin(), items.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (auto& [k, v] : items) { if (!first) o << ","; first = false; o << "\"" << esc(k) << "\":"; dump(*v, o); }
    o << "}";
  } else if (auto* a = n.as_array()) {
    o << "["; bool first = true; for (auto&& e : *a) { if (!first) o << ","; first = false; dump(e, o); } o << "]";
  } else if (auto v = n.template value<std::string>(); v && n.is_string()) { o << "s\"" << esc(*v) << "\""; }
  else if (n.is_boolean()) { o << (*n.template value<bool>() ? "true" : "false"); }
  else if (n.is_integer()) { o << "i" << *n.template value<int64_t>(); }
  else if (n.is_floating_point()) { double d = *n.template value<double>(); if (std::isnan(d)) o << "fnan"; else { char b[64]; std::snprintf(b, 64, "f%.17g", d); o << b; } }
  else o << "OTHER";
}
