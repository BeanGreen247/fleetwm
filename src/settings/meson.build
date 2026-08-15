# power_menu.cpp / accent_extract.cpp land later in Phase 6 (see docs/adr
# and the project plan roadmap). settings_window.cpp is the first real
# piece: a GTK4 front end over src/common/theme.{hpp,cpp}'s already-
# complete load/save logic.
executable(
  'fleetwm-settings',
  files(
    'main.cpp',
    'settings_window.cpp',
  ),
  include_directories: include_directories('.'),
  dependencies: [gtk4_dep, libcommon_dep],
  install: true,
)
