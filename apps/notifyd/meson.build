# GTK-free notification daemon (org.freedesktop.Notifications): sd-bus for the service, the fleetkit toolkit for the pop-up cards.
libsystemd_dep_notifyd = dependency('libsystemd')

executable(
  'fleetwm-notifyd',
  files('main.cpp', 'vtable.c'),
  include_directories: include_directories('.'),
  dependencies: [libfleetkit_dep, libsystemd_dep_notifyd, libcommon_core_dep],
  install: true,
)
