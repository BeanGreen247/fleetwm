#pragma once

// "Click again to close" for a popup program: the first run records its pid, a second run finds that
// process, asks it to quit and exits itself. Without this every click on the volume readout started
// another mixer (dozens piled up).

#include <signal.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <string>

namespace fleetwm {

// <XDG_RUNTIME_DIR>/<name>.pid, or /tmp when there is no runtime directory.
inline std::string single_instance_pid_file(const std::string& name) {
  const char* runtime = std::getenv("XDG_RUNTIME_DIR");
  return std::string(runtime && *runtime ? runtime : "/tmp") + "/" + name + ".pid";
}

// If the process recorded in `pidfile` is alive and really is `comm` (its name as /proc shows it, at most
// 15 characters), sends it SIGTERM and returns true: the caller should then just exit. A missing, stale or
// foreign pid (a recycled number now belonging to something else) returns false and kills nothing.
inline bool toggle_running_instance(const std::string& pidfile, const std::string& comm) {
  std::ifstream in(pidfile);
  long pid = 0;
  if (!(in >> pid) || pid <= 1 || pid == static_cast<long>(getpid())) return false;
  std::ifstream name_file("/proc/" + std::to_string(pid) + "/comm");
  std::string name;
  if (!(name_file >> name) || name != comm) return false;
  return kill(static_cast<pid_t>(pid), SIGTERM) == 0;
}

inline void write_pid_file(const std::string& pidfile) { std::ofstream(pidfile) << getpid() << "\n"; }

// Removes the file only when it still names this process (a newer instance may have replaced it).
inline void remove_pid_file(const std::string& pidfile) {
  std::ifstream in(pidfile);
  long pid = 0;
  if (in >> pid && pid == static_cast<long>(getpid())) {
    in.close();
    std::remove(pidfile.c_str());
  }
}

}  // namespace fleetwm
