#pragma once

// What a program the compositor, the bar or the launcher starts must NOT inherit from them. The event loops of these programs block
// SIGINT, SIGTERM and SIGCHLD (signalfd), ignore SIGPIPE and, in the helpers, SIGCHLD; the blocked mask and the "ignored" dispositions
// survive fork and exec. A terminal started that way had Ctrl+C blocked for every program run in it, and `git clone` failed with
// "waitpid for git-remote-https failed: No child processes" (found 2026-10-08 on the VM: SigBlk 0x4002 and SigIgn 0x384004 in the shell).
// Call reset_signals_for_exec() in the child between fork() and exec(), or pass a CleanSpawnAttr to posix_spawn.

#include <signal.h>
#include <spawn.h>

namespace fleetwm {

// Async-signal-safe: usable between fork() and exec().
inline void reset_signals_for_exec() {
  sigset_t none;
  sigemptyset(&none);
  sigprocmask(SIG_SETMASK, &none, nullptr);
  struct sigaction dfl {};
  dfl.sa_handler = SIG_DFL;
  sigemptyset(&dfl.sa_mask);
  for (int s : {SIGPIPE, SIGCHLD, SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGUSR1, SIGUSR2}) sigaction(s, &dfl, nullptr);
}

// A posix_spawn attribute with an empty signal mask and default dispositions for the signals above.
class CleanSpawnAttr {
 public:
  CleanSpawnAttr() {
    sigset_t none, dfl;
    sigemptyset(&none);
    sigemptyset(&dfl);
    for (int s : {SIGPIPE, SIGCHLD, SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGUSR1, SIGUSR2}) sigaddset(&dfl, s);
    posix_spawnattr_init(&attr_);
    posix_spawnattr_setsigmask(&attr_, &none);
    posix_spawnattr_setsigdefault(&attr_, &dfl);
    posix_spawnattr_setflags(&attr_, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
  }
  ~CleanSpawnAttr() { posix_spawnattr_destroy(&attr_); }
  CleanSpawnAttr(const CleanSpawnAttr&) = delete;
  CleanSpawnAttr& operator=(const CleanSpawnAttr&) = delete;
  const posix_spawnattr_t* get() const { return &attr_; }

 private:
  posix_spawnattr_t attr_;
};

}  // namespace fleetwm
