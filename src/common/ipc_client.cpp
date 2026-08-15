#include "ipc_client.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace fleetwm {

std::string ipc_socket_path() {
  const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
  std::string base = runtime_dir && *runtime_dir ? runtime_dir : "/tmp";
  return base + "/fleetwm.sock";
}

IpcClient::IpcClient() = default;

IpcClient::~IpcClient() {
  if (fd_ >= 0) {
    close(fd_);
  }
}

bool IpcClient::connect() {
  const std::string path = ipc_socket_path();

  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (fd < 0) {
    return false;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

  // Non-blocking connect: a Unix domain socket connect() to an existing
  // listening socket completes synchronously in practice, but we keep the
  // fd non-blocking throughout (see poll_lines) so the bar's mainloop never
  // stalls on IPC regardless of compositor responsiveness.
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 &&
      errno != EINPROGRESS) {
    close(fd);
    return false;
  }

  fd_ = fd;
  return true;
}

bool IpcClient::send_command(const std::string& line) {
  if (fd_ < 0) {
    return false;
  }
  std::string out = line;
  if (out.empty() || out.back() != '\n') {
    out += '\n';
  }
  ssize_t n = ::send(fd_, out.data(), out.size(), MSG_NOSIGNAL);
  return n == static_cast<ssize_t>(out.size());
}

void IpcClient::poll_lines(const std::function<void(const std::string&)>& on_line) {
  if (fd_ < 0) {
    return;
  }

  char buf[512];
  for (;;) {
    ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
    if (n > 0) {
      read_buffer_.append(buf, static_cast<size_t>(n));
      continue;
    }
    if (n == 0) {
      // Compositor closed the connection (e.g. restarted). Drop the fd;
      // the bar's reconnect timer (see bar/main.cpp) will retry.
      close(fd_);
      fd_ = -1;
    }
    break;  // n < 0: EAGAIN/EWOULDBLOCK or a real error either way stop here
  }

  size_t pos;
  while ((pos = read_buffer_.find('\n')) != std::string::npos) {
    on_line(read_buffer_.substr(0, pos));
    read_buffer_.erase(0, pos + 1);
  }
}

}  // namespace fleetwm
