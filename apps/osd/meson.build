# GTK-free volume and brightness pop-up (libsystemd for the logind brightness call); see main.cpp.
libsystemd_dep_osd = dependency('libsystemd')

executable(
  'fleetwm-osd',
  files('main.cpp'),
  include_directories: include_directories('.'),
  dependencies: [libfleetkit_dep, libsystemd_dep_osd, libcommon_core_dep],
  install: true,
)
