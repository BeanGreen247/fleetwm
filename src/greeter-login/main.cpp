#include <gtk/gtk.h>

#include <cstdlib>
#include <cstdio>

#include "login_window.hpp"
#include "clean_quit.hpp"

int main(int argc, char** argv) {
  const char* fd_env = std::getenv("FLEETWM_GREETER_IPC_FD");
  if (fd_env == nullptr) {
    std::fprintf(stderr, "fleetwm-greeter-login: FLEETWM_GREETER_IPC_FD not set\n");
    return 1;
  }
  int ipc_fd = std::atoi(fd_env);

  GtkApplication* app =
      gtk_application_new("dev.fleetwm.GreeterLogin", G_APPLICATION_DEFAULT_FLAGS);
  fleetwm::install_clean_quit(G_APPLICATION(app));

  fleetwm::greeter_login::LoginWindow window(app, ipc_fd);

  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
