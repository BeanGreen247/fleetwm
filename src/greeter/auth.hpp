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

// Runs the full PAM auth conversation on the current controlling terminal:
// prompts for username/password via tty::read_line()/read_password(),
// pam_authenticate(), pam_acct_mgmt(), and on success pam_open_session().
// Never logs the password anywhere.
AuthResult authenticate(const std::string& tty_name);

// Closes the PAM session opened by a successful authenticate() and ends the
// PAM transaction. Must be called exactly once per successful authenticate()
// call, after the launched session has fully exited.
void close_session(pam_handle_t* pamh, int session_open_status);

}  // namespace fleetwm::auth
