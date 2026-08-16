#include "auth.hpp"

#include <security/pam_appl.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace fleetwm::auth {

namespace {

constexpr const char* kServiceName = "fleetwm-greeter";

// Credentials the login UI already collected, handed to PAM's conversation
// callback via pam_conv's appdata pointer. `password_used` guards against
// answering more than one echo-off prompt with it -- a module chaining a
// second password-style prompt (e.g. a one-time token) should not silently
// get the login password fed back to it.
struct Credentials {
  const std::string* username;
  const std::string* password;
  bool password_used = false;
};

// PAM owns and frees both the response array and each response string it
// contains (see pam_conv(3)) -- we allocate with malloc/strdup accordingly.
int conv_callback(int num_msg, const struct pam_message** msgs, struct pam_response** out_resp,
                   void* appdata) {
  if (num_msg <= 0) {
    return PAM_CONV_ERR;
  }
  auto* creds = static_cast<Credentials*>(appdata);

  auto* resp = static_cast<pam_response*>(
      std::calloc(static_cast<size_t>(num_msg), sizeof(pam_response)));
  if (resp == nullptr) {
    return PAM_BUF_ERR;
  }

  for (int i = 0; i < num_msg; ++i) {
    const pam_message* msg = msgs[i];

    switch (msg->msg_style) {
      case PAM_PROMPT_ECHO_OFF: {
        // The password prompt. Answer with the already-collected password
        // exactly once; any further echo-off prompt (unexpected for this
        // greeter's plain password auth) gets an empty response rather
        // than reusing the password.
        const std::string& answer =
            (!creds->password_used) ? *creds->password : std::string();
        creds->password_used = true;
        resp[i].resp = strdup(answer.c_str());
        break;
      }
      case PAM_PROMPT_ECHO_ON: {
        // The username prompt -- normally never fires since PAM_USER is
        // set before pam_authenticate(), but answer it if some module
        // asks anyway rather than failing the whole conversation.
        resp[i].resp = strdup(creds->username->c_str());
        break;
      }
      case PAM_ERROR_MSG:
      case PAM_TEXT_INFO:
        // No TTY to print these to any more -- the login UI has no
        // channel for free-form PAM text messages today. Silently
        // discarded rather than failing the conversation over it.
        resp[i].resp = nullptr;
        break;
      default:
        resp[i].resp = nullptr;
        break;
    }
    resp[i].resp_retcode = 0;
  }

  *out_resp = resp;
  return PAM_SUCCESS;
}

}  // namespace

AuthResult authenticate(const std::string& tty_name, const std::string& username,
                         const std::string& password) {
  AuthResult result;

  Credentials creds{&username, &password};
  pam_conv conv{conv_callback, &creds};
  pam_handle_t* pamh = nullptr;

  int status = pam_start(kServiceName, username.c_str(), &conv, &pamh);
  if (status != PAM_SUCCESS) {
    return result;
  }

  pam_set_item(pamh, PAM_TTY, tty_name.c_str());

  status = pam_authenticate(pamh, 0);
  if (status != PAM_SUCCESS) {
    pam_end(pamh, status);
    return result;
  }

  status = pam_acct_mgmt(pamh, 0);
  if (status != PAM_SUCCESS) {
    pam_end(pamh, status);
    return result;
  }

  const void* username_raw = nullptr;
  status = pam_get_item(pamh, PAM_USER, &username_raw);
  if (status != PAM_SUCCESS || username_raw == nullptr) {
    pam_end(pamh, status);
    return result;
  }
  result.username = static_cast<const char*>(username_raw);

  status = pam_open_session(pamh, 0);
  if (status != PAM_SUCCESS) {
    pam_end(pamh, status);
    return result;
  }

  if (char** envlist = pam_getenvlist(pamh)) {
    for (char** e = envlist; *e != nullptr; ++e) {
      result.pam_envlist.emplace_back(*e);
      std::free(*e);
    }
    std::free(envlist);
  }

  result.ok = true;
  result.pamh = pamh;
  return result;
}

void close_session(pam_handle_t* pamh, int session_open_status) {
  if (pamh == nullptr) {
    return;
  }
  pam_close_session(pamh, 0);
  pam_end(pamh, session_open_status);
}

}  // namespace fleetwm::auth
