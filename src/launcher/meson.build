# GTK-free: lean toolkit (wayland-client + cairo + xkbcommon); .desktop files
# are parsed by lean's own desktop_entry module (no GIO).
executable(
  'fleetwm-launcher',
  files('main.cpp'),
  include_directories: include_directories('.'),
  dependencies: [liblean_dep, libcommon_core_dep],
  install: true,
)
