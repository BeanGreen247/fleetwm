#pragma once

// Blocking socket helpers shared by the VNC and RDP session threads.

#include <sys/socket.h>
#include <sys/types.h>

#include <time.h>

#include <arpa/inet.h>
#include <cstdarg>
#include <cstdio>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace fleetremote {

using Clock = std::chrono::steady_clock;

// CPU time this thread has used so far, in nanoseconds.
inline uint64_t thread_cpu_ns() {
  timespec ts;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
}

inline bool read_exact(int fd, void* buf, size_t n) {
  auto* p = static_cast<uint8_t*>(buf);
  while (n) {
    const ssize_t r = recv(fd, p, n, 0);
    if (r <= 0) return false;
    p += r;
    n -= static_cast<size_t>(r);
  }
  return true;
}

inline bool write_all(int fd, const void* buf, size_t n) {
  auto* p = static_cast<const uint8_t*>(buf);
  while (n) {
    const ssize_t r = send(fd, p, n, MSG_NOSIGNAL);
    if (r <= 0) return false;
    p += r;
    n -= static_cast<size_t>(r);
  }
  return true;
}

inline void put16be(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(uint8_t(x >> 8));
  v.push_back(uint8_t(x));
}
inline void put32be(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(uint8_t(x >> 24));
  v.push_back(uint8_t(x >> 16));
  v.push_back(uint8_t(x >> 8));
  v.push_back(uint8_t(x));
}
inline void put16le(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(uint8_t(x));
  v.push_back(uint8_t(x >> 8));
}
inline void put32le(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(uint8_t(x));
  v.push_back(uint8_t(x >> 8));
  v.push_back(uint8_t(x >> 16));
  v.push_back(uint8_t(x >> 24));
}

// One line on stderr (the session log) about a connection: who, and why it ended. Never carries a password.
inline void remote_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
inline void remote_log(const char* fmt, ...) {
  char buf[400];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  std::fprintf(stderr, "fleetwm-remote: %s\n", buf);
  std::fflush(stderr);
}

// The peer's address of a connected socket as text ("192.168.0.245"), "?" when unknown.
inline std::string peer_address(int fd) {
  sockaddr_storage ss{};
  socklen_t len = sizeof ss;
  char host[INET6_ADDRSTRLEN] = "?";
  if (getpeername(fd, reinterpret_cast<sockaddr*>(&ss), &len) == 0) {
    if (ss.ss_family == AF_INET) inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(&ss)->sin_addr, host, sizeof host);
    else if (ss.ss_family == AF_INET6) inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6*>(&ss)->sin6_addr, host, sizeof host);
  }
  return host;
}

// Compares in time that does not depend on where the strings differ.
inline bool same_secret(const std::string& a, const std::string& b) {
  unsigned diff = a.size() ^ b.size();
  for (size_t i = 0; i < a.size() && i < b.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  return diff == 0;
}

}  // namespace fleetremote
