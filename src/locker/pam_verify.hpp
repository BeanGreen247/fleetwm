#pragma once

#include <string>

namespace fleetwm::locker {

// Verifies `password` against the PAM "fleetwm-locker" service for the
// currently logged-in user (getuid()-derived, never taken from `password`'s
// caller) -- pam_authenticate() + pam_acct_mgmt() only, no
// pam_open_session()/pam_close_session(): unlike src/greeter/auth.cpp
// (which starts a brand-new login session), the lock screen is
// re-authenticating an *already-running* session, so there is no new
// session to open. Never logs the password; the conversation callback's
// own copy is not retained past the call.
bool verify_password(const std::string& username, const std::string& password);

}  // namespace fleetwm::locker
