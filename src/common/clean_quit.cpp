#include "clean_quit.hpp"

#include <glib-unix.h>

namespace fleetwm {

namespace {

gboolean on_quit_signal(gpointer user_data) {
  g_application_quit(static_cast<GApplication*>(user_data));
  return G_SOURCE_REMOVE;
}

}  // namespace

void install_clean_quit(GApplication* app) {
  g_unix_signal_add(SIGTERM, on_quit_signal, app);
  g_unix_signal_add(SIGINT, on_quit_signal, app);
}

}  // namespace fleetwm
