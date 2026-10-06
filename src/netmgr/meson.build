# Network backends (NetworkManager over D-Bus, wpa_supplicant over its control socket, and a
# read-only system status). GTK-free. Linked by fleetwm-settings and fleetwm-bar.
libsystemd_dep_netmgr = dependency('libsystemd')

libnetmgr = static_library(
  'fleetwm-netmgr',
  files(
    'network_parse.cpp',
    'system_devices.cpp',
    'wpa_backend.cpp',
    'nm_backend.cpp',
    'backend.cpp',
  ),
  include_directories: include_directories('.'),
  dependencies: [libsystemd_dep_netmgr],
)

libnetmgr_dep = declare_dependency(
  link_with: libnetmgr,
  include_directories: include_directories('.'),
  dependencies: [libsystemd_dep_netmgr],
)
