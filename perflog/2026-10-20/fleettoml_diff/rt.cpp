// round trip + random bytes under ASan/UBSan
#include "fleettoml.hpp"
#include <iostream>
#include <iterator>
#include <random>
#include <sstream>
static std::string canon(const toml::table& t) { return toml::to_string(t); }
int main() {
  std::string all((std::istreambuf_iterator<char>(std::cin)), {});
  size_t pos = 0; const std::string sep = "\x01\x01\x01\n"; size_t ok = 0, bad = 0, n = 0;
  while (pos <= all.size()) {
    size_t e = all.find(sep, pos); std::string c = all.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
    ++n;
    try { auto t = toml::parse(c); std::string p = canon(t); auto t2 = toml::parse(p); if (canon(t2) != p) { std::cout << "ROUNDTRIP MISMATCH\n" << c << "\n--\n" << p << "\n--\n" << canon(t2) << "\n"; ++bad; } else ++ok; }
    catch (const toml::parse_error&) {}
    if (e == std::string::npos) break; pos = e + sep.size();
  }
  std::mt19937 rng(5);
  for (int i = 0; i < 200000; ++i) { std::string s(rng() % 60, 0); for (auto& ch : s) { static const char cs[] = "[]{}=,.#\"' \n\\_-+0123456789abcxyzTZ:"; ch = cs[rng() % (sizeof cs - 1)]; } try { (void)toml::parse(s); } catch (const toml::parse_error&) {} }
  std::cout << "cases " << n << " roundtrip ok " << ok << " bad " << bad << "\n";
}
