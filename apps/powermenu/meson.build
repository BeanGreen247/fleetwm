# GTK-free: fleetkit toolkit (wayland-client + cairo + xkbcommon) + libsystemd
# (sd_pid_get_session for "Log out").
libsystemd_dep_powermenu = dependency('libsystemd')

executable(
  'fleetwm-powermenu',
  files('main.cpp'),
  include_directories: include_directories('.'),
  dependencies: [libfleetkit_dep, libsystemd_dep_powermenu, libcommon_core_dep],
  install: true,
)
