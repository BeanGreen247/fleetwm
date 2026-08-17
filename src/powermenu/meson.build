# Same gtk4-layer-shell-0 fallback dance as src/launcher/meson.build.
gtk4_dep_powermenu = dependency('gtk4')
gtk4_layer_shell_dep_powermenu = dependency('gtk4-layer-shell-0', required: false)
if not gtk4_layer_shell_dep_powermenu.found()
  gtk4_layer_shell_dep_powermenu = dependency('gtk4-layer-shell')
endif
libsystemd_dep_powermenu = dependency('libsystemd')

executable(
  'fleetwm-powermenu',
  files(
    'main.cpp',
    'power_menu_window.cpp',
  ),
  include_directories: include_directories('.'),
  dependencies: [gtk4_dep_powermenu, gtk4_layer_shell_dep_powermenu, libsystemd_dep_powermenu, libcommon_dep],
  install: true,
)
