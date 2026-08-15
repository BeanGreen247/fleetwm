#include "ipc_server.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "ipc_client.hpp"
#include "output.hpp"
#include "server.hpp"
#include "workspace.hpp"

namespace fleetwm {

namespace {

// wl_event_loop_add_fd callbacks are plain C function pointers with a
// void* userdata slot, same shape as wl_listener trampolines elsewhere in
// this compositor -- kept as free functions (declared friends in
// ipc_server.hpp) rather than member functions for that reason.

int ipc_server_handle_client_impl(IpcServer* self, int fd, uint32_t mask);

}  // namespace

int ipc_server_handle_accept(int fd, uint32_t mask, void* data) {
  (void)fd;
  (void)mask;
  static_cast<IpcServer*>(data)->accept_connection();
  return 0;
}

int ipc_server_handle_client(int fd, uint32_t mask, void* data) {
  return ipc_server_handle_client_impl(static_cast<IpcServer*>(data), fd, mask);
}

namespace {

int ipc_server_handle_client_impl(IpcServer* self, int fd, uint32_t mask) {
  if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
    self->drop_client(fd);
    return 0;
  }
  auto it = std::find_if(self->clients_.begin(), self->clients_.end(),
                          [fd](const IpcServer::Client& c) { return c.fd == fd; });
  if (it != self->clients_.end()) {
    self->handle_client_readable(*it);
  }
  return 0;
}

}  // namespace

IpcServer::IpcServer(Server* server) : server_(server) {}

IpcServer::~IpcServer() {
  for (Client& client : clients_) {
    wl_event_source_remove(client.source);
    close(client.fd);
  }
  if (listen_source_) {
    wl_event_source_remove(listen_source_);
  }
  if (listen_fd_ >= 0) {
    close(listen_fd_);
    unlink(ipc_socket_path().c_str());
  }
}

bool IpcServer::listen() {
  const std::string path = ipc_socket_path();
  unlink(path.c_str());  // stale socket from an unclean previous exit

  listen_fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (listen_fd_ < 0) {
    return false;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

  if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  if (::listen(listen_fd_, 16) < 0) {
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  wl_event_loop* loop = wl_display_get_event_loop(server_->display());
  listen_source_ = wl_event_loop_add_fd(loop, listen_fd_, WL_EVENT_READABLE,
                                         ipc_server_handle_accept, this);
  return true;
}

void IpcServer::accept_connection() {
  int fd = accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (fd < 0) {
    return;
  }

  wl_event_loop* loop = wl_display_get_event_loop(server_->display());
  wl_event_source* source = wl_event_loop_add_fd(
      loop, fd, WL_EVENT_READABLE, ipc_server_handle_client, this);

  clients_.push_back(Client{fd, source, {}});
}

void IpcServer::handle_client_readable(Client& client) {
  char buf[256];
  for (;;) {
    ssize_t n = recv(client.fd, buf, sizeof(buf), 0);
    if (n > 0) {
      client.read_buffer.append(buf, static_cast<size_t>(n));
      continue;
    }
    if (n == 0) {
      drop_client(client.fd);
      return;
    }
    break;  // EAGAIN or real error; either way nothing more to read now
  }

  size_t pos;
  while ((pos = client.read_buffer.find('\n')) != std::string::npos) {
    std::string line = client.read_buffer.substr(0, pos);
    client.read_buffer.erase(0, pos + 1);
    handle_line(client, line);
  }
}

void IpcServer::handle_line(Client& client, const std::string& line) {
  Workspace* active = server_->active_workspace_for_focused_output();

  if (line == "WORKSPACE?") {
    std::string reply = std::to_string(active ? active->index() : 0) + "\n";
    send(client.fd, reply.data(), reply.size(), MSG_NOSIGNAL);
    return;
  }

  if (line.rfind("WORKSPACE ", 0) == 0) {
    int index = -1;
    try {
      index = std::stoi(line.substr(10));
    } catch (...) {
      return;
    }
    if (index < 0 || index >= kWorkspaceCount || server_->outputs.empty()) {
      return;
    }
    // Phase 0 has no output-focus tracking yet (single-monitor dev setups
    // are the norm at this stage); route to the first output until Phase 1
    // wires real per-output focus-follows-cursor.
    server_->outputs.front()->switch_workspace(index);
    broadcast_workspace_changed(index);
  }
}

void IpcServer::broadcast_workspace_changed(int index) {
  std::string msg = "WORKSPACE_CHANGED " + std::to_string(index) + "\n";
  for (Client& client : clients_) {
    send(client.fd, msg.data(), msg.size(), MSG_NOSIGNAL);
  }
}

void IpcServer::drop_client(int fd) {
  auto it = std::find_if(clients_.begin(), clients_.end(),
                          [fd](const Client& c) { return c.fd == fd; });
  if (it == clients_.end()) {
    return;
  }
  wl_event_source_remove(it->source);
  close(it->fd);
  clients_.erase(it);
}

}  // namespace fleetwm
