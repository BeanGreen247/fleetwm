# Same gtk4-layer-shell-0 fallback dance as src/launcher/meson.build.
gtk4_dep_locker = dependency('gtk4')
gtk4_layer_shell_dep_locker = dependency('gtk4-layer-shell-0', required: false)
if not gtk4_layer_shell_dep_locker.found()
  gtk4_layer_shell_dep_locker = dependency('gtk4-layer-shell')
endif
pam_dep_locker = cpp.find_library('pam', required: get_option('greeter'))

executable(
  'fleetwm-locker',
  files(
    'main.cpp',
    'locker_window.cpp',
    'pam_verify.cpp',
  ),
  include_directories: include_directories('.'),
  dependencies: [gtk4_dep_locker, gtk4_layer_shell_dep_locker, pam_dep_locker, libcommon_dep],
  install: true,
)
