gtk4_dep = dependency('gtk4')
gtk4_layer_shell_dep = dependency('gtk4-layer-shell-0', required: false)
if not gtk4_layer_shell_dep.found()
  gtk4_layer_shell_dep = dependency('gtk4-layer-shell')
endif

# Volume% (ADR 0005): PipeWire event-driven subscription is primary, a
# polled `wpctl` subprocess is the fallback used only when PipeWire dev
# headers aren't available at build time -- same required:false pattern
# already used above for gtk4-layer-shell-0.
libpipewire_dep = dependency('libpipewire-0.3', required: false)

bar_deps = [gtk4_dep, gtk4_layer_shell_dep, libcommon_dep]
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
    'bar_window.cpp',
    'volume_source.cpp',
  ),
  include_directories: include_directories('.'),
  dependencies: bar_deps,
  cpp_args: bar_cpp_args,
  install: true,
)
