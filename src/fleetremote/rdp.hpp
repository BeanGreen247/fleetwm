#pragma once

#include "hub.hpp"
#include "tls.hpp"

namespace fleetremote {

// Runs one RDP client on `fd` until it disconnects, `Session::stop` is set or an error occurs; closes `fd`.
// With `options.tls` the security is TLS (the client sends the user name and password inside the TLS tunnel); without it standard RDP security with no encryption. No NLA.
void serve_rdp(Hub& hub, int fd, ServeOptions options);

}  // namespace fleetremote
