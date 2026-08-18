#include <gtk/gtk.h>

#include "power_menu_window.hpp"
#include "clean_quit.hpp"
#include "malloc_tuning.hpp"

int main(int argc, char** argv) {
  fleetwm::tune_malloc_for_low_rss();
  GtkApplication* app = gtk_application_new("dev.fleetwm.PowerMenu", G_APPLICATION_DEFAULT_FLAGS);
  fleetwm::install_clean_quit(G_APPLICATION(app));

  fleetwm::powermenu::PowerMenuWindow window(app);

  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
