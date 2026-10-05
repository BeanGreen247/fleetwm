# GTK-free: fleetkit (wayland-client + cairo + xkbcommon). login_ipc.{hpp,cpp}
# lives in src/greeter (shared with fleetwm-greet, the process that spawns this
# client) rather than being duplicated here.

executable(
  'fleetwm-greeter-login',
  files(
    'main.cpp',
    '../greeter/login_ipc.cpp',
  ),
  include_directories: include_directories('.', '../greeter'),
  dependencies: [libfleetkit_dep, libcommon_core_dep],
  install: true,
)
