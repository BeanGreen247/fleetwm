#include <toml++/toml.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
int main(int argc, char** argv) {
  std::ifstream f(argv[1]); std::stringstream ss; ss << f.rdbuf(); const std::string doc = ss.str(); const int n = atoi(argv[2]);
  size_t sink = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < n; ++i) { auto t = toml::parse(doc); sink += t.size(); }
  auto d = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / n;
  std::ostringstream o; auto t = toml::parse(doc);
  auto t1 = std::chrono::steady_clock::now();
  for (int i = 0; i < n; ++i) { std::ostringstream os; os << t; sink += os.str().size(); }
  auto w = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t1).count() / n;
  std::printf("parse %.1f us  print %.1f us (sink %zu)\n", d, w, sink);
}
