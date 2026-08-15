#include "session.hpp"

#include <grp.h>
#include <pwd.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>

namespace fleetwm::session {

namespace {

constexpr const char* kCompositorPath = "/usr/local/bin/fleetwm";

// Builds the child's environment: a minimal hardcoded floor, then whatever
// PAM provided layered on top (so PAM-provided values, e.g. from
// pam_systemd, can override the floor but the floor is always present).
std::vector<std::string> build_env(const passwd* pw,
                                    const std::vector<std::string>& pam_envlist) {
  std::vector<std::string> env;
  env.push_back(std::string("HOME=") + pw->pw_dir);
  env.push_back(std::string("USER=") + pw->pw_name);
  env.push_back(std::string("LOGNAME=") + pw->pw_name);
  env.push_back(std::string("SHELL=") + pw->pw_shell);
  env.emplace_back("PATH=/usr/local/bin:/usr/bin:/bin");
  env.emplace_back("XDG_SESSION_TYPE=wayland");

  for (const std::string& kv : pam_envlist) {
    env.push_back(kv);
  }
  return env;
}

}  // namespace

[[noreturn]] void exec_as_user(const passwd* pw,
                                const std::vector<std::string>& pam_envlist) {
  if (initgroups(pw->pw_name, pw->pw_gid) != 0) {
    std::perror("fleetwm-greet: initgroups failed");
    std::_Exit(1);
  }
  if (setgid(pw->pw_gid) != 0) {
    std::perror("fleetwm-greet: setgid failed");
    std::_Exit(1);
  }
  if (setuid(pw->pw_uid) != 0) {
    std::perror("fleetwm-greet: setuid failed");
    std::_Exit(1);
  }

  // Defense in depth: confirm the drop actually took before ever exec'ing.
  if (getuid() == 0 || geteuid() == 0 || getgid() == 0 || getegid() == 0) {
    std::fprintf(stderr, "fleetwm-greet: refusing to exec, still privileged after drop\n");
    std::_Exit(1);
  }

  if (chdir(pw->pw_dir) != 0) {
    // Non-fatal: fall back to running from wherever we are (e.g. "/") if
    // the user's home directory is missing or inaccessible.
    std::perror("fleetwm-greet: chdir to home failed, continuing");
  }

  std::vector<std::string> env_storage = build_env(pw, pam_envlist);
  std::vector<char*> envp;
  envp.reserve(env_storage.size() + 1);
  for (std::string& e : env_storage) {
    envp.push_back(e.data());
  }
  envp.push_back(nullptr);

  char* const argv[] = {const_cast<char*>(kCompositorPath), nullptr};

  execve(kCompositorPath, argv, envp.data());

  // execve only returns on failure.
  std::perror("fleetwm-greet: execve of compositor failed");
  std::_Exit(1);
}

}  // namespace fleetwm::session
