# power_menu.cpp / settings_window.cpp / accent_extract.cpp land in Phase 6
# (see docs/adr and the project plan roadmap). main.cpp is a placeholder
# stub until then so the tree builds end-to-end.
executable(
  'fleetwm-settings',
  files(
    'main.cpp',
  ),
  include_directories: include_directories('.'),
  dependencies: [gtk4_dep, gtk4_layer_shell_dep, libcommon_dep],
  install: true,
)
