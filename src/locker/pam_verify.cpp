#include "pam_verify.hpp"

#include <security/pam_appl.h>

#include <cstdlib>
#include <cstring>

namespace fleetwm::locker {

namespace {

constexpr const char* kServiceName = "fleetwm-locker";

// Same conversation-callback shape as src/greeter/auth.cpp's conv_callback
// -- kept as a separate small copy rather than shared, since this one has
// no username-prompt case (PAM_USER is always pre-set from getuid(), the
// lock screen never lets you type a different username) and no
// PAM_ERROR_MSG/PAM_TEXT_INFO handling (nothing to display it to here).
struct Credentials {
  const std::string* password;
  bool password_used = false;
};

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
    if (msgs[i]->msg_style == PAM_PROMPT_ECHO_OFF) {
      const std::string& answer = (!creds->password_used) ? *creds->password : std::string();
      creds->password_used = true;
      resp[i].resp = strdup(answer.c_str());
    } else {
      resp[i].resp = nullptr;
    }
    resp[i].resp_retcode = 0;
  }

  *out_resp = resp;
  return PAM_SUCCESS;
}

}  // namespace

bool verify_password(const std::string& username, const std::string& password) {
  Credentials creds{&password};
  pam_conv conv{conv_callback, &creds};
  pam_handle_t* pamh = nullptr;

  int status = pam_start(kServiceName, username.c_str(), &conv, &pamh);
  if (status != PAM_SUCCESS) {
    return false;
  }

  status = pam_authenticate(pamh, 0);
  if (status != PAM_SUCCESS) {
    pam_end(pamh, status);
    return false;
  }

  status = pam_acct_mgmt(pamh, 0);
  pam_end(pamh, status);
  return status == PAM_SUCCESS;
}

}  // namespace fleetwm::locker
