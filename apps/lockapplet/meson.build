# GTK-free: sd-bus (system tray item + org.freedesktop.ScreenSaver) and cairo (the padlock icon).
libsystemd_dep_lockapplet = dependency('libsystemd')
cairo_dep_lockapplet = dependency('cairo')

executable(
  'fleetwm-lockapplet',
  files('main.cpp', 'vtable.c'),
  include_directories: include_directories('.'),
  dependencies: [libcommon_core_dep, libsystemd_dep_lockapplet, cairo_dep_lockapplet],
  install: true,
)
