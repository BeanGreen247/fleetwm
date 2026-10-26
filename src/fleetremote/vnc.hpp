#pragma once

#include "hub.hpp"
#include "tls.hpp"

namespace fleetremote {

// Runs one VNC (RFB 3.8) client on `fd` until it disconnects, `Session::stop` is set or an error occurs; closes `fd`.
// Meant to be the body of a thread. The password counts to its first 8 characters, as in every VNC. With `options.tls` the
// viewer must use VeNCrypt (TLS, then the VNC password); without it the classic VNC login.
void serve_vnc(Hub& hub, int fd, ServeOptions options);

}  // namespace fleetremote
