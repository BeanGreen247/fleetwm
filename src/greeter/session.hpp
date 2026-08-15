#pragma once

#include <sys/types.h>

#include <string>
#include <vector>

struct passwd;

namespace fleetwm::session {

// Drops privileges to pw's uid/gid (initgroups, setgid, setuid -- in that
// order, checking every return value) and execve()s the fleetwm compositor
// as that user. Must only be called from a forked child that will do
// nothing else. Aborts the process (never execs) if any privilege-drop step
// fails. Never returns.
[[noreturn]] void exec_as_user(const passwd* pw,
                                const std::vector<std::string>& pam_envlist);

}  // namespace fleetwm::session
