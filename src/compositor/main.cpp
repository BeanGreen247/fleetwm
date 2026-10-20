#include <cstdio>
#include <cstdlib>

#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pthread.h>
#include <sys/resource.h>

#include "paths_config.h"
#include "prewarm.hpp"
#include "server.hpp"
#include "version.hpp"

int main(int argc, char** argv) {
  if (fleetwm::handle_info_flags(argc, argv, "fleetwm", "")) return 0;
  // The session environment preloads jemalloc for this process, where it pays
  // off (the compositor churns large buffers). The preload is already in effect
  // by now; dropping it from the environment keeps every child -- the bar,
  // wallpaper, and the applications the user starts -- on plain glibc, where
  // jemalloc would only add ~10 MB of resident arenas per small process.
  unsetenv("LD_PRELOAD");
  unsetenv("MALLOC_CONF");

  // Started by a display manager (lightdm, GDM, SDDM): the seat belongs to its logind session, which the manager switches to. libseat
  // would otherwise take the seatd that the login screen's service leaves running, get a seat that is not on screen, and the
  // compositor would fail every modeset ("gbm_bo_create failed") while the manager's greeter stays up.
  {
    const char* type = std::getenv("XDG_SESSION_TYPE");
    if (type && std::string_view(type) == "wayland" && std::getenv("XDG_SESSION_ID")) setenv("LIBSEAT_BACKEND", "logind", 0);
  }

  fleetwm::prewarm::start("fleetwm");  // learns this program's own manifest on its first run (see prewarm.hpp)
  // The programs the session starts a moment from now (the bar, the wallpaper, the lock applet): ask for their files now, so the reads
  // run while the compositor sets up its screens and are done by the time it forks them.
  {
    const char* bindir = std::getenv("FLEETWM_PREWARM_BINDIR");  // a test build run from its own directory
    std::vector<std::pair<std::string, std::string>> programs;
    for (const char* helper : {"fleetwm-bar", "fleetwm-wallpaper", "fleetwm-desktop", "fleetwm-lockapplet"})
      programs.emplace_back(std::string(bindir && *bindir ? bindir : FLEETWM_BINDIR) + "/" + helper, helper);
    fleetwm::prewarm::prewarm_programs_async(std::move(programs));
  }

  // Run ahead of ordinary processes so a busy build or browser never delays a frame or a
  // key press. Needs the nice limit installed by install.sh (limits.d/fleetwm.conf); without
  // it the request is refused and the compositor simply runs at normal priority.
  if (setpriority(PRIO_PROCESS, 0, -10) == 0) {
    // Only the compositor itself runs ahead: every program it starts goes back to normal.
    pthread_atfork(nullptr, nullptr, [] { setpriority(PRIO_PROCESS, 0, 0); });
  }

  fleetwm::Server server;

  if (!server.init()) {
    std::fprintf(stderr, "fleetwm: failed to initialize compositor\n");
    return 1;
  }

  server.run();
  return 0;
}
