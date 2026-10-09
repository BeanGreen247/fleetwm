#pragma once

#include <string>
#include <vector>

// Runs a helper program (udisksctl, gio, umount ...) without a shell, with a clean signal state, and returns what it
// printed. Behind an interface so the eject and mount logic is unit-tested with a fake.

namespace fleetwm::fm {

struct RunResult {
  int status = -1;        // exit status; -1 could not start; 128+n killed by signal n
  std::string output;     // stdout and stderr together
  bool timed_out = false;
};

class CommandRunner {
 public:
  virtual ~CommandRunner() = default;
  // `input` goes to the program's stdin (a password, for instance) and is never logged.
  virtual RunResult run(const std::vector<std::string>& argv, const std::string& input = {}, int timeout_ms = 30000) = 0;
};

// The real thing: posix_spawnp + poll.
CommandRunner& system_runner();

}  // namespace fleetwm::fm
