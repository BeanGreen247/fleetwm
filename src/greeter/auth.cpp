#include "auth.hpp"

#include <security/pam_appl.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "tty.hpp"

namespace fleetwm::auth {

namespace {

constexpr const char* kServiceName = "fleetwm-greeter";

// PAM owns and frees both the response array and each response string it
// contains (see pam_conv(3)) -- we allocate with malloc/strdup accordingly.
int conv_callback(int num_msg, const struct pam_message** msgs,
                   struct pam_response** out_resp, void* /*appdata*/) {
  if (num_msg <= 0) {
    return PAM_CONV_ERR;
  }

  auto* resp = static_cast<pam_response*>(
      std::calloc(static_cast<size_t>(num_msg), sizeof(pam_response)));
  if (resp == nullptr) {
    return PAM_BUF_ERR;
  }

  for (int i = 0; i < num_msg; ++i) {
    const pam_message* msg = msgs[i];
    std::string line;

    switch (msg->msg_style) {
      case PAM_PROMPT_ECHO_OFF: {
        tty::print(msg->msg ? msg->msg : "");
        if (!tty::read_password(line)) {
          for (int j = 0; j < i; ++j) {
            std::free(resp[j].resp);
          }
          std::free(resp);
          return PAM_CONV_ERR;
        }
        resp[i].resp = strdup(line.c_str());
        break;
      }
      case PAM_PROMPT_ECHO_ON: {
        tty::print(msg->msg ? msg->msg : "");
        if (!tty::read_line(line)) {
          for (int j = 0; j < i; ++j) {
            std::free(resp[j].resp);
          }
          std::free(resp);
          return PAM_CONV_ERR;
        }
        resp[i].resp = strdup(line.c_str());
        break;
      }
      case PAM_ERROR_MSG:
      case PAM_TEXT_INFO:
        if (msg->msg != nullptr) {
          tty::print_line(msg->msg);
        }
        resp[i].resp = nullptr;
        break;
      default:
        resp[i].resp = nullptr;
        break;
    }
    resp[i].resp_retcode = 0;
    // The password/line we just read was copied into the PAM response;
    // scrub our own copy now rather than waiting on the destructor.
    if (!line.empty()) {
      std::fill(line.begin(), line.end(), '\0');
    }
  }

  *out_resp = resp;
  return PAM_SUCCESS;
}

}  // namespace

AuthResult authenticate(const std::string& tty_name) {
  AuthResult result;

  pam_conv conv{conv_callback, nullptr};
  pam_handle_t* pamh = nullptr;

  tty::print_line("fleetwm login:");

  int status = pam_start(kServiceName, nullptr, &conv, &pamh);
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
