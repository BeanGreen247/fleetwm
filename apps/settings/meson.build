# GTK-free: fleetkit toolkit (wayland-client + cairo + xkbcommon + the immediate-
# mode Ui) plus the PipeWire-backed AudioMixer backend.
executable(
  'fleetwm-settings',
  files('main.cpp'),
  include_directories: include_directories('.'),
  dependencies: [libfleetkit_dep, libcommon_core_dep, libaudiomixer_dep],
  install: true,
)
