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
//   LOCK              -> spawn fleetwm-locker and enter the locked state
//                         (Server::request_lock(); no-op if already locked)
//   UNLOCK            -> exit the locked state, but ONLY if the sender is
//                         the exact process request_lock() spawned --
//                         verified via SO_PEERCRED, not sender-supplied
//                         data, so no other process on this socket can
//                         send this itself (see Server::confirm_unlock's
//                         doc comment, server.hpp)
//   LAYOUTS?          -> "LAYOUTS <current> <layout>:<variant> ..." (also broadcast whenever the
//                         current layout or the layout list changes)
//   LAYOUT_SET n      -> switch every keyboard to layout n
//   LAYOUT_NEXT       -> switch to the next layout
//   IDLE_INHIBIT 1|0  -> keep the screen on and the computer awake (1) or stop doing
//                         so (0) for as long as this connection stays open; the
//                         request goes away on its own when the sender exits
//   IDLE_INHIBITORS?  -> one "INHIBITOR wayland <pid> <program>" line per program
//                         holding an idle-inhibit request, then "END"
//
// Broadcast (unsolicited, sent to every connected client whenever a
// keybind-driven workspace switch happens, so the bar's highlighted
// workspace stays in sync even when the switch didn't originate from a
// bar click):
//
//   WORKSPACE_CHANGED N
//
// Broadcast whenever keyboard focus changes (see Server::focus_view --
// the single function responsible for every focus transition):
//
//   FOCUSED_TITLE <text>
//
// <text> is the newly-focused view's window title (falling back to its
// app_id, then an empty string, if no title is set). An empty <text>
// means no view currently holds focus.
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
  // Sends one line to every connected client (used for LAYOUTS).
  void broadcast_line(const std::string& line);
  // Tells every client the monitor set/modes/positions changed (re-query with OUTPUTS?).
  void broadcast_outputs_changed();

  // Sends "FOCUSED_TITLE <text>\n" to every currently-connected client.
  void broadcast_focused_title(const std::string& title);

  // Sends the formatted WINDOWS line to every client that sent SUBSCRIBE_WINDOWS
  // (skipped when unchanged since the last send).
  void broadcast_windows(const std::string& line);

 private:
  struct Client {
    int fd;
    wl_event_source* source;
    std::string read_buffer;
    bool windows_subscribed = false;
    bool idle_inhibiting = false;
  };

  void accept_connection();
  void handle_client_readable(Client& client);
  void handle_line(Client& client, const std::string& line);
  void drop_client(int fd);

  Server* server_;
  int listen_fd_ = -1;
  wl_event_source* listen_source_ = nullptr;
  std::vector<Client> clients_;
  std::string last_windows_line_;

  friend int ipc_server_handle_accept(int fd, uint32_t mask, void* data);
  friend int ipc_server_handle_client(int fd, uint32_t mask, void* data);
  friend int ipc_server_handle_client_impl(IpcServer* self, int fd, uint32_t mask);
};

}  // namespace fleetwm
