# GTK-free: lean toolkit (wayland-client + cairo + xkbcommon) + PAM.
pam_dep_locker = cpp.find_library('pam', required: get_option('greeter'))

executable(
  'fleetwm-locker',
  files(
    'main.cpp',
    'pam_verify.cpp',
  ),
  include_directories: include_directories('.'),
  dependencies: [liblean_dep, pam_dep_locker, libcommon_core_dep],
  install: true,
)
