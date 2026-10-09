#include "process.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>

#include "child_signals.hpp"

extern char** environ;

namespace fleetwm::fm {

namespace {
class SystemRunner : public CommandRunner {
 public:
  RunResult run(const std::vector<std::string>& argv, const std::string& input, int timeout_ms) override {
    RunResult r;
    if (argv.empty()) return r;
    int out_pipe[2], in_pipe[2];
    if (::pipe2(out_pipe, O_CLOEXEC) != 0) return r;
    if (::pipe2(in_pipe, O_CLOEXEC) != 0) {
      ::close(out_pipe[0]);
      ::close(out_pipe[1]);
      return r;
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, in_pipe[0], 0);
    posix_spawn_file_actions_adddup2(&fa, out_pipe[1], 1);
    posix_spawn_file_actions_adddup2(&fa, out_pipe[1], 2);
    CleanSpawnAttr attr;
    std::vector<char*> args;
    for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, args[0], &fa, attr.get(), args.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    ::close(out_pipe[1]);
    ::close(in_pipe[0]);
    if (rc != 0) {
      ::close(out_pipe[0]);
      ::close(in_pipe[1]);
      r.output = std::strerror(rc);
      return r;
    }
    if (!input.empty()) {
      const char* p = input.data();
      size_t n = input.size();
      ::fcntl(in_pipe[1], F_SETFL, O_NONBLOCK);
      while (n) {
        const ssize_t w = ::write(in_pipe[1], p, n);
        if (w <= 0) break;
        p += w;
        n -= static_cast<size_t>(w);
      }
    }
    ::close(in_pipe[1]);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    char buf[4096];
    for (;;) {
      const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
      if (left <= 0) {
        r.timed_out = true;
        ::kill(pid, SIGKILL);
        break;
      }
      pollfd pfd{out_pipe[0], POLLIN, 0};
      const int pr = ::poll(&pfd, 1, static_cast<int>(left));
      if (pr < 0 && errno == EINTR) continue;
      if (pr <= 0) continue;
      const ssize_t n = ::read(out_pipe[0], buf, sizeof buf);
      if (n > 0) r.output.append(buf, static_cast<size_t>(n));
      else if (n == 0) break;
      else if (errno != EINTR) break;
    }
    ::close(out_pipe[0]);
    int st = 0;
    while (::waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    r.status = WIFEXITED(st) ? WEXITSTATUS(st) : (WIFSIGNALED(st) ? 128 + WTERMSIG(st) : -1);
    return r;
  }
};
}  // namespace

CommandRunner& system_runner() {
  static SystemRunner r;
  return r;
}

}  // namespace fleetwm::fm
