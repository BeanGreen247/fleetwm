#include <pwd.h>
#include <security/pam_appl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "auth.hpp"
#include "session.hpp"
#include "tty.hpp"

namespace {

int run_login_cycle(const std::string& tty_name) {
  fleetwm::auth::AuthResult result = fleetwm::auth::authenticate(tty_name);
  if (!result.ok) {
    fleetwm::tty::print_line("Login incorrect");
    return 1;
  }

  errno = 0;
  passwd* pw = getpwnam(result.username.c_str());
  if (pw == nullptr) {
    fleetwm::tty::print_line("fleetwm-greet: could not resolve user record");
    fleetwm::auth::close_session(result.pamh, PAM_SUCCESS);
    return 1;
  }
  // Copy out of the static getpwnam() buffer before it can be clobbered by
  // any further PAM/libc calls between here and the fork.
  passwd pw_copy = *pw;

  pid_t child = fork();
  if (child < 0) {
    std::perror("fleetwm-greet: fork failed");
    fleetwm::auth::close_session(result.pamh, PAM_SUCCESS);
    return 1;
  }

  if (child == 0) {
    fleetwm::session::exec_as_user(&pw_copy, result.pam_envlist);
    // Unreachable: exec_as_user never returns.
  }

  int wstatus = 0;
  waitpid(child, &wstatus, 0);

  fleetwm::auth::close_session(result.pamh, PAM_SUCCESS);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (!isatty(STDIN_FILENO)) {
    std::fprintf(stderr, "fleetwm-greet: stdin is not a tty\n");
    return 1;
  }

  std::string tty_name = argc > 1 ? argv[1] : "";

  for (;;) {
    fleetwm::tty::clear_screen();
    run_login_cycle(tty_name);
  }
}
