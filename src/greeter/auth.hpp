#pragma once

#include <security/pam_appl.h>

#include <string>
#include <vector>

namespace fleetwm::auth {

struct AuthResult {
  bool ok = false;
  std::string username;
  std::vector<std::string> pam_envlist;
  // Opaque PAM handle, owned by the caller until passed to close_session().
  // Only valid when ok is true.
  pam_handle_t* pamh = nullptr;
};

// Runs the full PAM auth conversation for a username/password pair already
// collected by the login UI (src/greeter-login, via login_ipc): sets
// PAM_USER up front so PAM's own username prompt never fires, answers
// exactly one PAM_PROMPT_ECHO_OFF (password) prompt with `password`, then
// pam_authenticate(), pam_acct_mgmt(), and on success pam_open_session().
// Never logs the password anywhere; the password is scrubbed from the
// conversation callback's own copy immediately after PAM consumes it.
AuthResult authenticate(const std::string& tty_name, const std::string& username,
                         const std::string& password);

// Closes the PAM session opened by a successful authenticate() and ends the
// PAM transaction. Must be called exactly once per successful authenticate()
// call, after the launched session has fully exited.
void close_session(pam_handle_t* pamh, int session_open_status);

}  // namespace fleetwm::auth
