#pragma once

#include <wayland-server-core.h>

#include <string>
#include <vector>

namespace fleetwm {

class Server;

// Server side of the compositor control socket (client side: IpcClient in
// src/common/ipc_client.hpp; both agree on ipc_socket_path()). Wire
// protocol, line-based ASCII:
//
//   WORKSPACE N       -> switch the focused output's active workspace to N
//                         (N in 0-9; 0 is the '0' key / 10th workspace)
//   WORKSPACE?        -> reply "N\n" with the focused output's active
//                         workspace
//
// Broadcast (unsolicited, sent to every connected client whenever a
// keybind-driven workspace switch happens, so the bar's highlighted
// workspace stays in sync even when the switch didn't originate from a
// bar click):
//
//   WORKSPACE_CHANGED N
//
// Everything here runs on the compositor's own wl_event_loop via
// wl_event_loop_add_fd -- no separate thread -- since wlroots/wl_display
// state is not safe to touch off the main event loop thread.
class IpcServer {
 public:
  explicit IpcServer(Server* server);
  ~IpcServer();

  bool listen();

  // Sends "WORKSPACE_CHANGED N\n" to every currently-connected client.
  void broadcast_workspace_changed(int index);

 private:
  struct Client {
    int fd;
    wl_event_source* source;
    std::string read_buffer;
  };

  void accept_connection();
  void handle_client_readable(Client& client);
  void handle_line(Client& client, const std::string& line);
  void drop_client(int fd);

  Server* server_;
  int listen_fd_ = -1;
  wl_event_source* listen_source_ = nullptr;
  std::vector<Client> clients_;

  friend int ipc_server_handle_accept(int fd, uint32_t mask, void* data);
  friend int ipc_server_handle_client(int fd, uint32_t mask, void* data);
  friend int ipc_server_handle_client_impl(IpcServer* self, int fd, uint32_t mask);
};

}  // namespace fleetwm
