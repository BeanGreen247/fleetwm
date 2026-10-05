#include <cstdio>
#include <cstdlib>

#include "server.hpp"

int main() {
  // The session environment preloads jemalloc for this process, where it pays
  // off (the compositor churns large buffers). The preload is already in effect
  // by now; dropping it from the environment keeps every child -- the bar,
  // wallpaper, and the applications the user starts -- on plain glibc, where
  // jemalloc would only add ~10 MB of resident arenas per small process.
  unsetenv("LD_PRELOAD");
  unsetenv("MALLOC_CONF");

  fleetwm::Server server;

  if (!server.init()) {
    std::fprintf(stderr, "fleetwm: failed to initialize compositor\n");
    return 1;
  }

  server.run();
  return 0;
}
