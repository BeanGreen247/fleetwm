# Bluetooth: BlueZ over the system D-Bus (sd-bus), the same way the network back end talks to NetworkManager. GTK-free.
# Linked by fleetwm-settings and fleetwm-bar.
libsystemd_dep_bt = dependency('libsystemd')

libbtmgr = static_library(
  'fleetwm-btmgr',
  files('bt_types.cpp', 'bt_backend.cpp'),
  include_directories: include_directories('.'),
  dependencies: [libsystemd_dep_bt],
)

libbtmgr_dep = declare_dependency(
  link_with: libbtmgr,
  include_directories: include_directories('.'),
  dependencies: [libsystemd_dep_bt],
)
