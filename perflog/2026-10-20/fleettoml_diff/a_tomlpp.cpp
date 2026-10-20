#include <algorithm>
#include <vector>
#include <toml++/toml.h>
#include "dump.inc"
int main(int argc, char** argv) {
  std::string line; std::string all((std::istreambuf_iterator<char>(std::cin)), {});
  // cases separated by a line "\x01\x01\x01\n"
  size_t pos = 0; const std::string sep = "\x01\x01\x01\n";
  while (pos <= all.size()) {
    size_t e = all.find(sep, pos); std::string c = all.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
    try { auto t = toml::parse(c); std::ostringstream o; o << "{"; bool first = true;
      std::vector<std::pair<std::string,const toml::node*>> items; for (auto&& [k,v] : t) items.push_back({std::string(k.str()), &v});
      std::sort(items.begin(), items.end(), [](auto&a, auto&b){return a.first<b.first;});
      for (auto& [k,v] : items) { if(!first) o<<","; first=false; o<<"\""<<esc(k)<<"\":"; dump(*v,o);} o<<"}"; std::cout << o.str() << "\n"; }
    catch (const toml::parse_error&) { std::cout << "ERROR\n"; }
    if (e == std::string::npos) break; pos = e + sep.size();
  }
}
