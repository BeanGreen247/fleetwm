gtk4_dep = dependency('gtk4')
gtk4_layer_shell_dep = dependency('gtk4-layer-shell-0', required: false)
if not gtk4_layer_shell_dep.found()
  gtk4_layer_shell_dep = dependency('gtk4-layer-shell')
endif
gio_dep = dependency('gio-2.0')

executable(
  'fleetwm-launcher',
  files(
    'main.cpp',
    'app_index.cpp',
    'launcher_window.cpp',
  ),
  include_directories: include_directories('.'),
  dependencies: [gtk4_dep, gtk4_layer_shell_dep, gio_dep],
  install: true,
)
