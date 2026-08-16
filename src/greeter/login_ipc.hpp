#pragma once

#include <cstdint>
#include <string>

// Tiny blocking protocol carried over a UNIX socketpair between
// fleetwm-greet (root, owns PAM auth + the wlroots display) and
// fleetwm-greeter-login (the GTK4 login-card client it spawns as a child,
// also root pre-auth -- see docs/adr/0006 and docs/adr/0007). Deliberately
// hand-rolled rather than pulling in a serialization library: two message
// shapes each way, never versioned independently of the two binaries that
// are always built and deployed together.
namespace fleetwm::greeter_ipc {

enum class ClientMsgType : uint8_t {
  LoginAttempt = 1,
  PowerAction = 2,
};

enum class ServerMsgType : uint8_t {
  AuthFailed = 1,
};

struct ClientMessage {
  ClientMsgType type = ClientMsgType::LoginAttempt;
  std::string username;  // LoginAttempt only
  std::string password;  // LoginAttempt only
  std::string action;    // PowerAction only: "poweroff" or "reboot"
};

struct ServerMessage {
  ServerMsgType type = ServerMsgType::AuthFailed;
  std::string message;  // AuthFailed only: shown inline on the login card
};

// All return false on a short write/read or EOF/error -- callers should
// treat that as "the other end is gone" and stop the session, not retry.
bool send_login_attempt(int fd, const std::string& username, const std::string& password);
bool send_power_action(int fd, const std::string& action);
bool send_auth_failed(int fd, const std::string& message);

bool recv_client_message(int fd, ClientMessage& out);
bool recv_server_message(int fd, ServerMessage& out);

}  // namespace fleetwm::greeter_ipc
