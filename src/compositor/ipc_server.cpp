#include "ipc_server.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <sstream>
#include <cerrno>
#include <cstring>

#include "ipc_client.hpp"
#include "output.hpp"
#include "view.hpp"
#include "server.hpp"
#include "workspace.hpp"

namespace fleetwm {

// wl_event_loop_add_fd callbacks are plain C function pointers with a
// void* userdata slot, same shape as wl_listener trampolines elsewhere in
// this compositor -- kept as free functions (declared friends in
// ipc_server.hpp) rather than member functions for that reason.

int ipc_server_handle_client_impl(IpcServer* self, int fd, uint32_t mask);

int ipc_server_handle_accept(int fd, uint32_t mask, void* data) {
  (void)fd;
  (void)mask;
  static_cast<IpcServer*>(data)->accept_connection();
  return 0;
}

int ipc_server_handle_client(int fd, uint32_t mask, void* data) {
  return ipc_server_handle_client_impl(static_cast<IpcServer*>(data), fd, mask);
}

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

  clients_.push_back(Client{fd, source, {}, false});
}

void IpcServer::handle_client_readable(Client& client) {
  char buf[4096];  // 1 MB flood: 3.3 ms with 256-byte reads, 0.29 ms with 4096
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

  // Walk with a head offset and erase once: erasing per line is quadratic for a flood (1 MB: 0.50 s against 0.9 ms).
  size_t head = 0, pos;
  while ((pos = client.read_buffer.find('\n', head)) != std::string::npos) {
    std::string line = client.read_buffer.substr(head, pos - head);
    head = pos + 1;
    handle_line(client, line);
  }
  client.read_buffer.erase(0, head);
}

void IpcServer::handle_line(Client& client, const std::string& line) {
  Workspace* active = server_->active_workspace_for_focused_output();

  if (line == "WORKSPACE?") {
    std::string reply = std::to_string(active ? active->index() : 0) + "\n";
    send(client.fd, reply.data(), reply.size(), MSG_NOSIGNAL);
    return;
  }

  if (line == "OUTPUTS?") {
    // One block per monitor, then END:
    //   OUTPUT <name> <x> <y> <width> <height> <refresh_mhz>
    //   MODE <width> <height> <refresh_mhz> <current 0|1> <preferred 0|1>   (repeated)
    std::string reply;
    reply += "DISPLAY " + server_->display_settings().primary_output + " " +
             (server_->display_settings().taskbar_all_displays ? "1" : "0") + "\n";
    for (const Server::OutputInfo& o : server_->describe_outputs()) {
      reply += "OUTPUT " + o.name + " " + std::to_string(o.x) + " " + std::to_string(o.y) + " " +
               std::to_string(o.width) + " " + std::to_string(o.height) + " " +
               std::to_string(o.refresh_mhz) + "\n";
      for (const Server::ModeInfo& m : o.modes) {
        reply += "MODE " + std::to_string(m.width) + " " + std::to_string(m.height) + " " +
                 std::to_string(m.refresh_mhz) + " " + (m.current ? "1" : "0") + " " +
                 (m.preferred ? "1" : "0") + "\n";
      }
    }
    reply += "END\n";
    send(client.fd, reply.data(), reply.size(), MSG_NOSIGNAL);
    return;
  }
  if (line.rfind("DISPLAY_SET ", 0) == 0) {
    std::istringstream in(line.substr(12));
    DisplaySettings setting;
    int all = 1;
    std::string reply;
    if (in >> setting.primary_output >> all) {
      setting.taskbar_all_displays = all != 0;
      std::string error;
      reply = server_->apply_display_settings(setting, &error) ? "OK\n" : "ERR " + error + "\n";
    } else {
      reply = "ERR malformed DISPLAY_SET\n";
    }
    send(client.fd, reply.data(), reply.size(), MSG_NOSIGNAL);
    return;
  }

  // Test hooks for plugging and unplugging a screen on a headless session (FLEETWM_DEBUG_OUTPUTS=1 in the compositor's
  // environment; a real session ignores them): DEBUG_OUTPUT_ADD <width> <height> and DEBUG_OUTPUT_REMOVE <name>.
  if (line.rfind("DEBUG_OUTPUT_", 0) == 0) {
    std::string reply = "ERR debug outputs are off\n";
    if (std::getenv("FLEETWM_DEBUG_OUTPUTS")) {
      std::istringstream in(line.substr(13));
      std::string verb, name;
      int w = 1280, h = 720;
      in >> verb;
      if (verb == "ADD") {
        in >> w >> h;
        reply = server_->debug_add_output(w, h) ? "OK\n" : "ERR not a headless session\n";
      } else if (verb == "REMOVE" && (in >> name)) {
        reply = server_->debug_remove_output(name) ? "OK\n" : "ERR no such output\n";
      }
    }
    send(client.fd, reply.data(), reply.size(), MSG_NOSIGNAL);
    return;
  }
  if (line.rfind("OUTPUT_SET ", 0) == 0) {
    // OUTPUT_SET <name> <width> <height> <refresh_mhz> <x> <y>
    // width/height 0 = keep the current mode; x/y of -999999 = keep the position.
    std::istringstream in(line.substr(11));
    std::string name;
    OutputSetting setting;
    int x = 0, y = 0;
    std::string reply;
    if (in >> name >> setting.width >> setting.height >> setting.refresh_mhz >> x >> y) {
      constexpr int kKeep = -999999;
      if (x != kKeep && y != kKeep) {
        setting.has_pos = true;
        setting.x = x;
        setting.y = y;
      }
      std::string error;
      reply = server_->apply_output_setting(name, setting, &error) ? "OK\n" : "ERR " + error + "\n";
    } else {
      reply = "ERR malformed OUTPUT_SET\n";
    }
    send(client.fd, reply.data(), reply.size(), MSG_NOSIGNAL);
    return;
  }

  if (line == "SUBSCRIBE_WINDOWS") {
    client.windows_subscribed = true;
    std::string reply = format_window_list(server_->window_snapshot()) + "\n";
    send(client.fd, reply.data(), reply.size(), MSG_NOSIGNAL);
    return;
  }

  if (line.rfind("WINDOW_", 0) == 0) {
    // WINDOW_ACTIVATE|TOGGLE|MINIMIZE|CLOSE <id>
    const size_t sp = line.find(' ');
    if (sp == std::string::npos) return;
    const std::string verb = line.substr(0, sp);
    View* view = nullptr;
    try {
      view = server_->view_by_id(static_cast<uint32_t>(std::stoul(line.substr(sp + 1))));
    } catch (...) {
      return;
    }
    if (!view) return;
    if (verb == "WINDOW_ACTIVATE") server_->activate_view(view);
    else if (verb == "WINDOW_TOGGLE") server_->toggle_view_from_taskbar(view);
    else if (verb == "WINDOW_MINIMIZE") view->set_minimized(true);
    else if (verb == "WINDOW_CLOSE") view->close();
    return;
  }

  if (line == "IDLE_INHIBIT 1" || line == "IDLE_INHIBIT 0") {
    const bool on = line.back() == '1';
    if (on != client.idle_inhibiting) {
      client.idle_inhibiting = on;
      server_->ipc_idle_inhibit(on);
    }
    return;
  }

  if (line == "LAYOUTS?") {
    const std::string reply = server_->layouts_line() + "\n";
    send(client.fd, reply.data(), reply.size(), MSG_NOSIGNAL);
    return;
  }
  if (line == "LAYOUT_NEXT") {
    server_->step_layout(1);
    return;
  }
  if (line.rfind("LAYOUT_SET ", 0) == 0) {
    try {
      server_->set_layout(std::stoi(line.substr(11)));
    } catch (...) {
    }
    return;
  }

  if (line == "IDLE_INHIBITORS?") {
    const std::string reply = server_->idle_inhibitor_report() + "END\n";
    send(client.fd, reply.data(), reply.size(), MSG_NOSIGNAL);
    return;
  }

  if (line == "LOCK") {
    server_->request_lock();
    return;
  }

  if (line == "UNLOCK") {
    // SO_PEERCRED gives the kernel's own record of which process is on
    // the other end of this specific socket fd -- unlike the pid a
    // client could claim in its own message text, this cannot be
    // spoofed by some other process just connecting and sending
    // "UNLOCK" itself (see Server::confirm_unlock's doc comment,
    // server.hpp). Only fleetwm-locker's exact spawned pid, checked by
    // Server::confirm_unlock, can ever unlock a locked session this way.
    struct ucred cred {};
    socklen_t cred_len = sizeof(cred);
    if (getsockopt(client.fd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) == 0) {
      server_->confirm_unlock(cred.pid);
    }
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
    server_->switch_workspace_everywhere(index);  // the bar that was clicked is on the screen the pointer is on
    broadcast_workspace_changed(index);
  }
}

void IpcServer::broadcast_workspace_changed(int index) {
  std::string msg = "WORKSPACE_CHANGED " + std::to_string(index) + "\n";
  for (Client& client : clients_) {
    send(client.fd, msg.data(), msg.size(), MSG_NOSIGNAL);
  }
}

void IpcServer::broadcast_line(const std::string& line) {
  const std::string msg = line + "\n";
  for (Client& client : clients_) send(client.fd, msg.data(), msg.size(), MSG_NOSIGNAL);
}

void IpcServer::broadcast_outputs_changed() {
  const std::string msg = "OUTPUTS_CHANGED\n";
  for (Client& client : clients_) {
    send(client.fd, msg.data(), msg.size(), MSG_NOSIGNAL);
  }
}

void IpcServer::broadcast_focused_title(const std::string& title) {
  // A window title is arbitrary client-supplied text and could in theory
  // contain a newline, which would corrupt the line-based protocol (the
  // embedded '\n' would be read as the end of the command). Replace any
  // with a space rather than trusting client input to stay one line.
  std::string sanitized = title;
  std::replace(sanitized.begin(), sanitized.end(), '\n', ' ');
  std::string msg = "FOCUSED_TITLE " + sanitized + "\n";
  for (Client& client : clients_) {
    send(client.fd, msg.data(), msg.size(), MSG_NOSIGNAL);
  }
}

void IpcServer::broadcast_windows(const std::string& line) {
  if (line == last_windows_line_) return;
  last_windows_line_ = line;
  const std::string msg = line + "\n";
  for (Client& client : clients_) {
    if (client.windows_subscribed) {
      send(client.fd, msg.data(), msg.size(), MSG_NOSIGNAL);
    }
  }
}

void IpcServer::drop_client(int fd) {
  auto it = std::find_if(clients_.begin(), clients_.end(),
                          [fd](const Client& c) { return c.fd == fd; });
  if (it == clients_.end()) {
    return;
  }
  if (it->idle_inhibiting) server_->ipc_idle_inhibit(false);
  wl_event_source_remove(it->source);
  close(it->fd);
  clients_.erase(it);
}

}  // namespace fleetwm
