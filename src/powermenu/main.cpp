#include <gtk/gtk.h>

#include "power_menu_window.hpp"
#include "clean_quit.hpp"

int main(int argc, char** argv) {
  GtkApplication* app = gtk_application_new("dev.fleetwm.PowerMenu", G_APPLICATION_DEFAULT_FLAGS);
  fleetwm::install_clean_quit(G_APPLICATION(app));

  fleetwm::powermenu::PowerMenuWindow window(app);

  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
