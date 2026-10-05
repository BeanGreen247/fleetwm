# GTK-free: fleetkit toolkit (wayland-client + cairo + xkbcommon); .desktop files
# are parsed by fleetkit's own desktop_entry module (no GIO).
executable(
  'fleetwm-launcher',
  files('main.cpp'),
  include_directories: include_directories('.'),
  dependencies: [libfleetkit_dep, libcommon_core_dep],
  install: true,
)
