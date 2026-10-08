#include <gtest/gtest.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "child_signals.hpp"

extern char** environ;

namespace fleetwm {
namespace {

// Starts `grep -E SigBlk|SigIgn /proc/self/status` and returns its two hex masks.
bool child_masks(const posix_spawnattr_t* attr, unsigned long* blocked, unsigned long* ignored, bool use_fork_reset = false) {
  int fds[2];
  if (pipe(fds) != 0) return false;
  pid_t pid = -1;
  if (use_fork_reset) {
    pid = fork();
    if (pid == 0) {
      reset_signals_for_exec();
      dup2(fds[1], STDOUT_FILENO);
      execlp("grep", "grep", "-E", "SigBlk|SigIgn", "/proc/self/status", nullptr);
      _exit(127);
    }
  } else {
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], STDOUT_FILENO);
    char* argv[] = {const_cast<char*>("grep"), const_cast<char*>("-E"), const_cast<char*>("SigBlk|SigIgn"), const_cast<char*>("/proc/self/status"), nullptr};
    if (posix_spawnp(&pid, "grep", &fa, attr, argv, environ) != 0) pid = -1;
    posix_spawn_file_actions_destroy(&fa);
  }
  close(fds[1]);
  std::string out;
  char buf[256];
  ssize_t n;
  while ((n = read(fds[0], buf, sizeof buf)) > 0) out.append(buf, n);
  close(fds[0]);
  int st = 0;
  if (pid > 0) waitpid(pid, &st, 0);
  *blocked = *ignored = ~0ul;
  const size_t b = out.find("SigBlk:"), i = out.find("SigIgn:");
  if (b == std::string::npos || i == std::string::npos) return false;
  *blocked = std::strtoul(out.c_str() + b + 7, nullptr, 16);
  *ignored = std::strtoul(out.c_str() + i + 7, nullptr, 16);
  return true;
}

// The state the compositor and the bar are in: quit signals and SIGCHLD blocked, SIGPIPE and SIGCHLD ignored.
struct DirtyProcessState {
  sigset_t old_mask;
  struct sigaction old_pipe, old_chld;
  DirtyProcessState() {
    sigset_t m;
    sigemptyset(&m);
    for (int s : {SIGINT, SIGTERM, SIGCHLD}) sigaddset(&m, s);
    pthread_sigmask(SIG_BLOCK, &m, &old_mask);
    struct sigaction ign {};
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    sigaction(SIGPIPE, &ign, &old_pipe);
    sigaction(SIGCHLD, &ign, &old_chld);
  }
  ~DirtyProcessState() {
    sigaction(SIGPIPE, &old_pipe, nullptr);
    sigaction(SIGCHLD, &old_chld, nullptr);
    pthread_sigmask(SIG_SETMASK, &old_mask, nullptr);
  }
};

constexpr unsigned long bit(int sig) { return 1ul << (sig - 1); }

TEST(ChildSignals, WithoutTheResetAChildInheritsBlockedAndIgnoredSignals) {
  DirtyProcessState dirty;
  unsigned long blocked = 0, ignored = 0;
  ASSERT_TRUE(child_masks(nullptr, &blocked, &ignored));
  EXPECT_NE(blocked & bit(SIGINT), 0ul) << "this is the bug: Ctrl+C blocked in everything started from the bar or the compositor";
  EXPECT_NE(ignored & bit(SIGPIPE), 0ul);
}

TEST(ChildSignals, SpawnAttrGivesTheChildACleanMaskAndDefaultDispositions) {
  DirtyProcessState dirty;
  CleanSpawnAttr attr;
  unsigned long blocked = ~0ul, ignored = ~0ul;
  ASSERT_TRUE(child_masks(attr.get(), &blocked, &ignored));
  EXPECT_EQ(blocked, 0ul);
  EXPECT_EQ(ignored & (bit(SIGPIPE) | bit(SIGCHLD) | bit(SIGINT) | bit(SIGTERM)), 0ul);
}

TEST(ChildSignals, ResetBetweenForkAndExecDoesTheSame) {
  DirtyProcessState dirty;
  unsigned long blocked = ~0ul, ignored = ~0ul;
  ASSERT_TRUE(child_masks(nullptr, &blocked, &ignored, true));
  EXPECT_EQ(blocked, 0ul);
  EXPECT_EQ(ignored & (bit(SIGPIPE) | bit(SIGCHLD)), 0ul);
}

}  // namespace
}  // namespace fleetwm
