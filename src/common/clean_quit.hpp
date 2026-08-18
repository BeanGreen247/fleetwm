#pragma once

#include <gio/gio.h>

namespace fleetwm {

// Installs SIGTERM/SIGINT handlers that call g_application_quit(app)
// instead of the default disposition (abrupt process termination).
// Mirrors the compositor's own SIGTERM/SIGINT handling (server.cpp) --
// every fleetwm GTK4 client (bar, wallpaper, settings, launcher, locker,
// powermenu, greeter-login) previously had none, so `systemctl stop`,
// `kill -TERM`, or any other clean-shutdown path skipped GLib's normal
// application teardown (and, notably, a profiling build's atexit-based
// gcov flush -- see scripts/build-pgo-auto.sh, which needs every client
// to actually reach exit() to keep its training data).
void install_clean_quit(GApplication* app);

}  // namespace fleetwm
