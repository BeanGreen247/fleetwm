#include "login_ipc.hpp"

#include <sys/types.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>

namespace fleetwm::greeter_ipc {

namespace {

bool write_all(int fd, const void* data, size_t len) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  size_t written = 0;
  while (written < len) {
    ssize_t n = ::write(fd, p + written, len - written);
    if (n <= 0) {
      return false;
    }
    written += static_cast<size_t>(n);
  }
  return true;
}

bool read_all(int fd, void* data, size_t len) {
  uint8_t* p = static_cast<uint8_t*>(data);
  size_t got = 0;
  while (got < len) {
    ssize_t n = ::read(fd, p + got, len - got);
    if (n <= 0) {
      return false;
    }
    got += static_cast<size_t>(n);
  }
  return true;
}

bool write_string(int fd, const std::string& s) {
  uint32_t len = static_cast<uint32_t>(s.size());
  if (!write_all(fd, &len, sizeof(len))) {
    return false;
  }
  return len == 0 || write_all(fd, s.data(), len);
}

// 1 MiB is far larger than any username/password/message this protocol
// ever carries -- just a sanity ceiling against a corrupted length prefix
// turning into an unbounded allocation.
constexpr uint32_t kMaxStringLen = 1u << 20;

bool read_string(int fd, std::string& out) {
  uint32_t len = 0;
  if (!read_all(fd, &len, sizeof(len)) || len > kMaxStringLen) {
    return false;
  }
  out.resize(len);
  return len == 0 || read_all(fd, out.data(), len);
}

}  // namespace

bool send_login_attempt(int fd, const std::string& username, const std::string& password) {
  uint8_t type = static_cast<uint8_t>(ClientMsgType::LoginAttempt);
  return write_all(fd, &type, sizeof(type)) && write_string(fd, username) &&
         write_string(fd, password);
}

bool send_power_action(int fd, const std::string& action) {
  uint8_t type = static_cast<uint8_t>(ClientMsgType::PowerAction);
  return write_all(fd, &type, sizeof(type)) && write_string(fd, action);
}

bool send_auth_failed(int fd, const std::string& message) {
  uint8_t type = static_cast<uint8_t>(ServerMsgType::AuthFailed);
  return write_all(fd, &type, sizeof(type)) && write_string(fd, message);
}

bool recv_client_message(int fd, ClientMessage& out) {
  uint8_t type = 0;
  if (!read_all(fd, &type, sizeof(type))) {
    return false;
  }
  switch (static_cast<ClientMsgType>(type)) {
    case ClientMsgType::LoginAttempt:
      out.type = ClientMsgType::LoginAttempt;
      return read_string(fd, out.username) && read_string(fd, out.password);
    case ClientMsgType::PowerAction:
      out.type = ClientMsgType::PowerAction;
      return read_string(fd, out.action);
  }
  return false;
}

bool recv_server_message(int fd, ServerMessage& out) {
  uint8_t type = 0;
  if (!read_all(fd, &type, sizeof(type))) {
    return false;
  }
  switch (static_cast<ServerMsgType>(type)) {
    case ServerMsgType::AuthFailed:
      out.type = ServerMsgType::AuthFailed;
      return read_string(fd, out.message);
  }
  return false;
}

}  // namespace fleetwm::greeter_ipc
