#pragma once

#include <functional>
#include <string>

namespace fleetwm {

// Path to the compositor's control socket. Resolves under
// $XDG_RUNTIME_DIR, per the standard Wayland/systemd convention for
// per-user, per-session runtime state.
std::string ipc_socket_path();

// A small blocking-connect, line-based client for the compositor's IPC
// protocol (see docs/adr/0003-ipc-design.md for the wire format:
// newline-terminated ASCII commands, e.g. "WORKSPACE 3\n").
//
// The bar keeps one IpcClient open for its whole lifetime and polls it via
// its underlying fd from the GTK/GLib mainloop (see bar/workspace_widget.cpp)
// rather than blocking the UI thread on read().
class IpcClient {
 public:
  IpcClient();
  ~IpcClient();

  IpcClient(const IpcClient&) = delete;
  IpcClient& operator=(const IpcClient&) = delete;

  // Connects to the compositor socket. Returns false (rather than throwing)
  // on failure so callers -- particularly the bar -- can degrade gracefully
  // (e.g. show workspace buttons as inert) if the compositor isn't up yet
  // or IPC isn't available, instead of crashing the whole panel process.
  bool connect();

  bool is_connected() const { return fd_ >= 0; }
  int fd() const { return fd_; }

  // Sends a single command line (a trailing '\n' is appended if missing).
  // Returns false on write failure.
  bool send_command(const std::string& line);

  // Reads and dispatches any complete lines currently available on the
  // socket without blocking. Call this from a GLib IO watch on fd().
  // `on_line` is invoked once per complete line (without the trailing
  // newline).
  void poll_lines(const std::function<void(const std::string&)>& on_line);

 private:
  int fd_ = -1;
  std::string read_buffer_;
};

}  // namespace fleetwm
