#include <gtk/gtk.h>

#include "settings_window.hpp"
#include "clean_quit.hpp"

int main(int argc, char** argv) {
  GtkApplication* app =
      gtk_application_new("dev.fleetwm.Settings", G_APPLICATION_DEFAULT_FLAGS);
  fleetwm::install_clean_quit(G_APPLICATION(app));

  fleetwm::settings::SettingsWindow window(app);

  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
