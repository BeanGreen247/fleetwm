# fleetwm-desktop: the desktop of the Desktop layout (icons, the Windows 7 style right-click menu) and, in the Tiling layout, a small card that
# says which keys show the shortcuts. One per screen, started by the compositor with --output NAME. GTK-free: fleetkit + cairo.
executable(
  'fleetwm-desktop',
  files('main.cpp'),
  include_directories: include_directories('.'),
  dependencies: [libfleetfm_icons_dep, libfleetfm_core_dep, libfleetkit_dep, libcommon_core_dep],
  install: true,
)
