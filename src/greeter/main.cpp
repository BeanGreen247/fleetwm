#include <pwd.h>
#include <security/pam_appl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "auth.hpp"
#include "compositor.hpp"
#include "login_ipc.hpp"
#include "session.hpp"

namespace {

constexpr const char* kLoginClientPath = "/usr/local/bin/fleetwm-greeter-login";

// Fire-and-forget shutdown/reboot -- fleetwm-greet already runs as root for
// its whole lifetime (ADR 0006), so this is a direct systemctl call, no
// polkit prompt needed (there is no session/UI for polkit to prompt on
// yet anyway).
void execute_power_action(const std::string& action) {
  pid_t pid = fork();
  if (pid == 0) {
    const char* verb = action == "reboot" ? "reboot" : "poweroff";
    execl("/usr/bin/systemctl", "systemctl", verb, nullptr);
    std::perror("fleetwm-greet: failed to exec systemctl");
    _exit(1);
  } else if (pid < 0) {
    std::perror("fleetwm-greet: fork for power action failed");
  }
}

// Spawns the login-card GTK4 client (src/greeter-login), pointed at the
// greeter compositor's internal Wayland socket and handed its end of the
// credentials IPC socketpair. Returns the child pid, or -1 on fork
// failure. Still root at this point, same privilege tier as the rest of
// this process pre-auth -- see docs/adr/0006 and docs/adr/0007.
pid_t spawn_login_client(const std::string& wayland_socket, int ipc_fd) {
  pid_t pid = fork();
  if (pid != 0) {
    return pid;
  }
  setenv("WAYLAND_DISPLAY", wayland_socket.c_str(), 1);
  char fd_buf[16];
  std::snprintf(fd_buf, sizeof(fd_buf), "%d", ipc_fd);
  setenv("FLEETWM_GREETER_IPC_FD", fd_buf, 1);
  execl(kLoginClientPath, kLoginClientPath, nullptr);
  std::perror("fleetwm-greet: failed to exec fleetwm-greeter-login");
  _exit(1);
}

// Runs one full login cycle: brings up a fresh greeter compositor + login
// client, waits for either a successful authentication or the login
// client dying/disconnecting, then hands off to the authenticated user's
// session. Always returns (never execve's itself) -- main()'s for(;;) is
// what restarts a fresh cycle after a failed/aborted attempt, mirroring
// the old TTY loop's "never actually returns to a dead screen" contract.
// Returns false on any failure before a login screen ever got shown (init
// failure, socketpair failure, fork failure) -- main()'s loop backs off
// briefly on false rather than retrying instantly, see the comment there
// for why that matters.
bool run_login_cycle(const std::string& tty_name) {
  fleetwm::auth::AuthResult auth_result;
  bool authenticated = false;

  {
    fleetwm::greeter::GreeterCompositor compositor;
    std::string wayland_socket = compositor.init();
    if (wayland_socket.empty()) {
      std::fprintf(stderr, "fleetwm-greet: failed to initialize greeter compositor\n");
      return false;
    }

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
      std::perror("fleetwm-greet: socketpair failed");
      return false;
    }

    pid_t login_pid = spawn_login_client(wayland_socket, sv[1]);
    close(sv[1]);
    if (login_pid < 0) {
      close(sv[0]);
      return false;
    }

    int ipc_fd = sv[0];
    auto on_ipc_readable = [&]() {
      fleetwm::greeter_ipc::ClientMessage msg;
      if (!fleetwm::greeter_ipc::recv_client_message(ipc_fd, msg)) {
        compositor.stop();
        return;
      }
      if (msg.type == fleetwm::greeter_ipc::ClientMsgType::LoginAttempt) {
        // Root login is graphical-greeter policy, not a PAM restriction --
        // root is expected to be a last-resort terminal/rescue-shell
        // login, never a routine desktop session (explicit product
        // requirement). Rejected here, before ever touching PAM, rather
        // than relying on the picker (src/greeter-login/login_window.cpp)
        // simply not listing root as a tile -- the "Other User" field
        // still accepts typed input, and this is the actual trust
        // boundary regardless of what the client-side UI offers.
        if (msg.username == "root") {
          fleetwm::greeter_ipc::send_auth_failed(ipc_fd, "Root login is not permitted here");
          return;
        }
        auth_result = fleetwm::auth::authenticate(tty_name, msg.username, msg.password);
        if (auth_result.ok) {
          authenticated = true;
          compositor.stop();
        } else {
          fleetwm::greeter_ipc::send_auth_failed(ipc_fd, "Login incorrect");
        }
      } else if (msg.type == fleetwm::greeter_ipc::ClientMsgType::PowerAction) {
        execute_power_action(msg.action);
      }
    };

    compositor.run(ipc_fd, on_ipc_readable);

    // Login client's job is done either way (successful login or the
    // compositor loop otherwise stopped) -- kill it explicitly rather
    // than letting it race the compositor's own teardown below for the
    // Wayland display going away.
    kill(login_pid, SIGTERM);
    int wstatus = 0;
    waitpid(login_pid, &wstatus, 0);
    close(ipc_fd);

    // `compositor` destructs at the end of this block, releasing DRM
    // master *before* run_login_cycle ever decides to fork+exec into the
    // real fleetwm compositor below -- DRM master is exclusive, so the
    // handoff must not overlap with this process still holding it.
  }

  if (!authenticated) {
    if (auth_result.pamh) {
      fleetwm::auth::close_session(auth_result.pamh, PAM_SUCCESS);
    }
    return true;
  }

  errno = 0;
  passwd* pw = getpwnam(auth_result.username.c_str());
  if (pw == nullptr) {
    fleetwm::auth::close_session(auth_result.pamh, PAM_SUCCESS);
    return true;
  }
  // Copy out of the static getpwnam() buffer before it can be clobbered by
  // any further PAM/libc calls between here and the fork.
  passwd pw_copy = *pw;

  pid_t child = fork();
  if (child < 0) {
    std::perror("fleetwm-greet: fork failed");
    fleetwm::auth::close_session(auth_result.pamh, PAM_SUCCESS);
    return true;
  }

  if (child == 0) {
    fleetwm::session::exec_as_user(&pw_copy, auth_result.pam_envlist);
    // Unreachable: exec_as_user never returns.
  }

  int wstatus = 0;
  waitpid(child, &wstatus, 0);
  fleetwm::auth::close_session(auth_result.pamh, PAM_SUCCESS);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::string tty_name = argc > 1 ? argv[1] : "";
  for (;;) {
    // On failure (no login screen ever got shown -- e.g. DRM/seat init
    // failed), back off before retrying instead of spinning immediately.
    // Confirmed via real testing that an instant-retry loop here is a
    // real problem, not just wasted CPU: each failed wlr_backend_
    // autocreate() attempt leaks a handful of fds/udev objects that
    // never get cleaned up until the *process* exits (not just this
    // function returning), so a tight failure loop exhausts fds within
    // seconds and turns an initial, recoverable failure (e.g. seatd not
    // up yet at boot) into a hard "udev_enumerate_scan_devices failed"
    // wall that a slow retry would have sailed past cleanly.
    if (!run_login_cycle(tty_name)) {
      sleep(1);
    }
  }
}
