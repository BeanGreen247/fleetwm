#include <cstdio>

#include "server.hpp"

int main() {
  fleetwm::Server server;

  if (!server.init()) {
    std::fprintf(stderr, "fleetwm: failed to initialize compositor\n");
    return 1;
  }

  server.run();
  return 0;
}
