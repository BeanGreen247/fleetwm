# GTK-free: fleetkit toolkit (wayland-client + cairo + xkbcommon), sd-bus for the
# system tray, optional PipeWire for the volume readout.
libpipewire_dep = dependency('libpipewire-0.3', required: false)
libsystemd_dep_bar = dependency('libsystemd')

bar_deps = [libfleetkit_dep, libcommon_core_dep, libsystemd_dep_bar, libnetmgr_dep]
bar_cpp_args = []
if libpipewire_dep.found()
  bar_deps += libpipewire_dep
  bar_cpp_args += '-DFLEETWM_HAVE_PIPEWIRE=1'
else
  bar_cpp_args += '-DFLEETWM_HAVE_PIPEWIRE=0'
endif

executable(
  'fleetwm-bar',
  files(
    'main.cpp',
    'volume_source.cpp',
    'tray.cpp',
    'tray_vtable.c',
  ),
  include_directories: include_directories('.'),
  dependencies: bar_deps,
  cpp_args: bar_cpp_args,
  install: true,
)
