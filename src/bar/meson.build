# GTK-free: fleetkit toolkit (wayland-client + cairo + xkbcommon), sd-bus for the
# system tray, optional PipeWire for the volume readout.
libpipewire_dep = dependency('libpipewire-0.3', required: false)
libsystemd_dep_bar = dependency('libsystemd')
xcb_dep_bar = dependency('xcb')
xcb_composite_dep_bar = dependency('xcb-composite')  # the X11 (Wine) tray icons

bar_deps = [xcb_dep_bar, xcb_composite_dep_bar, libfleetkit_dep, libcommon_core_dep, libsystemd_dep_bar, libnetmgr_dep, libbtmgr_dep]
bar_cpp_args = []
bar_sources = ['main.cpp', 'mpris.cpp', 'volume_source.cpp', 'spectrum_source.cpp', 'tray.cpp', 'tray_vtable.c', 'xembed_tray.cpp']
if libpipewire_dep.found()
  bar_deps += libpipewire_dep
  bar_cpp_args += '-DFLEETWM_HAVE_PIPEWIRE=1'
else
  bar_cpp_args += '-DFLEETWM_HAVE_PIPEWIRE=0'
endif

executable(
  'fleetwm-bar',
  files(bar_sources),
  include_directories: include_directories('.'),
  dependencies: bar_deps,
  cpp_args: bar_cpp_args,
  install: true,
)
