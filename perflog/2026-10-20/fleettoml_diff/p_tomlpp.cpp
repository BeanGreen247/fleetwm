#include <toml++/toml.h>
#include <iostream>
#include <iterator>
int main() {
  std::string all((std::istreambuf_iterator<char>(std::cin)), {});
  size_t pos = 0; const std::string sep = "\x01\x01\x01\n";
  while (pos <= all.size()) {
    size_t e = all.find(sep, pos); std::string c = all.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
    try { auto t = toml::parse(c); std::cout << "<<" << t << ">>\n"; } catch (const toml::parse_error&) { std::cout << "ERROR\n"; }
    if (e == std::string::npos) break; pos = e + sep.size();
  }
}
