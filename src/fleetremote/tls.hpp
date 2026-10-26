#pragma once

#include <openssl/ssl.h>

#include <string>

namespace fleetremote {

// The TLS context both servers use: the self-signed certificate in `dir` (cert.pem, key.pem), created on first use.
// Returns null (message on stderr) when it cannot be made.
SSL_CTX* make_tls_context(const std::string& dir);

// What a session thread needs to know about its listener.
struct ServeOptions {
  std::string user;      // RDP only: the login name
  std::string password;
  SSL_CTX* tls = nullptr;  // set: the viewer must use TLS (VeNCrypt for VNC, TLS security for RDP); null: no encryption
};

}  // namespace fleetremote
