// fleetwm-ctl: asks the running compositor a question or gives it a command over its IPC socket, and prints the answer. For scripts and the game test bed.
//   fleetwm-ctl frame-stats [reset]   how evenly frames reach each screen (interval percentiles, late and hitched frames, the compositor's commit cost)
//   fleetwm-ctl scanout on|off|auto   direct scanout for fullscreen windows, for comparing (auto follows game.toml)
//   fleetwm-ctl windows               the window list: id, flags, workspace@output, app id, title (tab separated)
//   fleetwm-ctl close ID              asks a window to close
//   fleetwm-ctl raw "COMMAND"         any IPC line
#include <poll.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "version.hpp"
#include "ipc_client.hpp"

namespace {

// Prints the lines the compositor sends until `stop` matches one (a prefix) or nothing arrives for `idle_ms`.
int ask(const std::string& command, const char* stop_prefix, int idle_ms, bool all_lines) {
  fleetwm::IpcClient ipc;
  if (!ipc.connect()) {
    std::fprintf(stderr, "fleetwm-ctl: no compositor to talk to\n");
    return 1;
  }
  if (!ipc.send_command(command)) return 1;
  bool done = false;
  while (!done) {
    pollfd p{ipc.fd(), POLLIN, 0};
    if (poll(&p, 1, idle_ms) <= 0) break;
    ipc.poll_lines([&](const std::string& line) {
      if (done) return;
      if (stop_prefix && line.rfind(stop_prefix, 0) == 0) {
        std::puts(line.c_str());
        done = true;
        return;
      }
      if (all_lines && line != "END") std::puts(line.c_str());
      if (line == "END") done = true;
    });
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (fleetwm::handle_info_flags(argc, argv, "fleetwm-ctl", "frame-stats [reset] | scanout on|off|auto | windows | close ID | raw COMMAND")) return 0;
  const std::string verb = argc > 1 ? argv[1] : "";
  if (verb == "frame-stats") return ask(argc > 2 && std::string(argv[2]) == "reset" ? "FRAME_STATS RESET" : "FRAME_STATS", nullptr, 1000, true);
  if (verb == "scanout" && argc > 2) return ask(std::string("SCANOUT ") + argv[2], nullptr, 500, true);
  if (verb == "windows") return ask("SUBSCRIBE_WINDOWS", "WINDOWS", 1000, false);
  if (verb == "close" && argc > 2) return ask(std::string("WINDOW_CLOSE ") + argv[2], nullptr, 100, true);
  if (verb == "raw" && argc > 2) return ask(argv[2], nullptr, 500, true);
  std::fprintf(stderr, "usage: fleetwm-ctl frame-stats [reset] | windows | close ID | raw COMMAND\n");
  return 2;
}
