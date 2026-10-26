#pragma once

// A viewer's connection: the socket, plus TLS once it has been started. Blocking reads and writes, as the session
// threads want them. Used by both servers so that TLS is a switch, not a second code path.

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

#include "net_util.hpp"

namespace fleetremote {

class Conn {
 public:
  explicit Conn(int fd) : fd_(fd), peer_(peer_address(fd)) {}
  ~Conn() {
    if (ssl_) {
      SSL_shutdown(ssl_);
      SSL_free(ssl_);
    }
    close(fd_);
  }
  Conn(const Conn&) = delete;
  Conn& operator=(const Conn&) = delete;

  int fd() const { return fd_; }
  // Every byte read or written from now on is added to these (the session's totals).
  void count_into(std::atomic<uint64_t>* tx, std::atomic<uint64_t>* rx) {
    tx_ = tx;
    rx_ = rx;
  }
  bool tls() const { return ssl_ != nullptr; }

  // Server side TLS handshake on the open socket.
  bool start_tls(SSL_CTX* ctx) {
    ssl_ = SSL_new(ctx);
    if (!ssl_) return false;
    SSL_set_fd(ssl_, fd_);
    if (SSL_accept(ssl_) == 1) return true;
    char err[160] = "the client closed the connection";
    if (const unsigned long e = ERR_get_error()) ERR_error_string_n(e, err, sizeof err);
    remote_log("%s: TLS handshake failed (%s). Usual causes: the client does not trust the self-signed certificate (tell it to ignore or accept it), or it insists on NLA "
               "(Network Level Authentication), which this server does not offer", peer_.c_str(), err);
    return false;
  }

  bool read_exact(void* buf, size_t n) {
    if (rx_) rx_->fetch_add(n, std::memory_order_relaxed);
    if (!ssl_) return fleetremote::read_exact(fd_, buf, n);
    auto* p = static_cast<uint8_t*>(buf);
    while (n) {
      const int r = SSL_read(ssl_, p, static_cast<int>(std::min<size_t>(n, 1 << 20)));
      if (r <= 0) return false;
      p += r;
      n -= static_cast<size_t>(r);
    }
    return true;
  }

  bool write_all(const void* buf, size_t n) {
    if (tx_) tx_->fetch_add(n, std::memory_order_relaxed);
    if (!ssl_) return fleetremote::write_all(fd_, buf, n);
    auto* p = static_cast<const uint8_t*>(buf);
    while (n) {
      const int r = SSL_write(ssl_, p, static_cast<int>(std::min<size_t>(n, 1 << 20)));
      if (r <= 0) return false;
      p += r;
      n -= static_cast<size_t>(r);
    }
    return true;
  }
  bool write_all(const std::vector<uint8_t>& v) { return write_all(v.data(), v.size()); }

  // Bytes TLS has already decrypted and the socket will not report again.
  bool buffered() const { return ssl_ && SSL_pending(ssl_) > 0; }

 private:
  int fd_;
  std::string peer_;  // taken at the start: a socket the client already closed cannot say who it was
  SSL* ssl_ = nullptr;
  std::atomic<uint64_t>* tx_ = nullptr;
  std::atomic<uint64_t>* rx_ = nullptr;
};

}  // namespace fleetremote
