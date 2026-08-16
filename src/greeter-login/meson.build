# gtk4_dep_common (declared in src/common/meson.build) covers GTK4 itself;
# login_ipc.{hpp,cpp} lives in src/greeter (shared with fleetwm-greet, the
# process that spawns this client) rather than being duplicated here.

executable(
  'fleetwm-greeter-login',
  files(
    'main.cpp',
    'login_window.cpp',
    '../greeter/login_ipc.cpp',
  ),
  include_directories: include_directories('.', '../greeter'),
  dependencies: [gtk4_dep_common, libcommon_dep],
  install: true,
)
